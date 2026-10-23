#include "aeatchaincheck.h"

#include "aeathash.h"

#include <QHash>
#include <QSet>
#include <QXmlStreamReader>

namespace AeatChainCheck {

namespace {

const char kDateFormat[] = "dd-MM-yyyy";

struct ParsedRecord {
    bool valid = false;
    bool cancellation = false;
    QHash<QString, QString> leaves;   // "Parent/Element" -> text, local names
    QString get(const QString &key) const { return leaves.value(key).trimmed(); }
};

ParsedRecord parse(const QString &xml)
{
    ParsedRecord out;
    QXmlStreamReader reader(xml);
    QStringList path;
    QString text;
    bool hasChild = false;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            if (path.isEmpty()) {
                const QString root = reader.name().toString();
                out.cancellation = (root == QLatin1String("RegistroAnulacion"));
                out.valid = out.cancellation || root == QLatin1String("RegistroAlta");
            }
            path << reader.name().toString();
            text.clear();
            hasChild = false;
        } else if (reader.isCharacters()) {
            text += reader.text().toString();
        } else if (reader.isEndElement()) {
            const bool leaf = !hasChild;
            const QString name = path.takeLast();
            if (leaf && !path.isEmpty())
                out.leaves.insert(path.last() + QLatin1Char('/') + name, text);
            hasChild = true;   // the parent now has a child element
            text.clear();
        }
    }
    if (reader.hasError())
        out.valid = false;
    return out;
}

QString recomputedHash(const ParsedRecord &r)
{
    const QString root = r.cancellation ? QStringLiteral("RegistroAnulacion") : QStringLiteral("RegistroAlta");
    const QString previous = r.get(QStringLiteral("RegistroAnterior/Huella"));
    const QString generatedAt = r.get(root + QStringLiteral("/FechaHoraHusoGenRegistro"));
    if (r.cancellation) {
        AeatHash::CancellationFields f;
        f.issuerNif     = r.get(QStringLiteral("IDFactura/IDEmisorFacturaAnulada"));
        f.invoiceNumber = r.get(QStringLiteral("IDFactura/NumSerieFacturaAnulada"));
        f.issueDate     = QDate::fromString(r.get(QStringLiteral("IDFactura/FechaExpedicionFacturaAnulada")),
                                            QLatin1String(kDateFormat));
        f.previousHash  = previous;
        f.generatedAt   = generatedAt;
        return AeatHash::cancellationHash(f);
    }
    AeatHash::RegistrationFields f;
    f.issuerNif     = r.get(QStringLiteral("IDFactura/IDEmisorFactura"));
    f.invoiceNumber = r.get(QStringLiteral("IDFactura/NumSerieFactura"));
    f.issueDate     = QDate::fromString(r.get(QStringLiteral("IDFactura/FechaExpedicionFactura")),
                                        QLatin1String(kDateFormat));
    f.invoiceType   = r.get(root + QStringLiteral("/TipoFactura"));
    f.totalTax      = r.get(root + QStringLiteral("/CuotaTotal"));
    f.totalAmount   = r.get(root + QStringLiteral("/ImporteTotal"));
    f.previousHash  = previous;
    f.generatedAt   = generatedAt;
    return AeatHash::registrationHash(f);
}

QString issuerOf(const ParsedRecord &r)
{
    return r.get(r.cancellation ? QStringLiteral("IDFactura/IDEmisorFacturaAnulada")
                                : QStringLiteral("IDFactura/IDEmisorFactura"));
}

QString storedHashOf(const ParsedRecord &r)
{
    return r.get((r.cancellation ? QStringLiteral("RegistroAnulacion") : QStringLiteral("RegistroAlta"))
                 + QStringLiteral("/Huella"));
}

Result fail(int index, int checked, const QString &problem)
{
    Result r;
    r.ok = false;
    r.failedIndex = index;
    r.checked = checked;
    r.problem = problem;
    return r;
}

} // namespace

Result verify(const QStringList &recordXmls, const QSet<QString> *anchors)
{
    QList<ParsedRecord> records;
    QHash<QString, QSet<QString>> knownHashes;   // issuer -> every stored huella
    for (const QString &xml : recordXmls) {
        records << parse(xml);
        const ParsedRecord &r = records.last();
        knownHashes[issuerOf(r)].insert(storedHashOf(r).toUpper());
    }
    Result out;
    QHash<QString, QString> lastHash;            // issuer -> huella of its previous record here
    for (int i = 0; i < records.size(); ++i) {
        const ParsedRecord &r = records.at(i);
        const int checked = i + 1;
        if (!r.valid)
            return fail(i, checked, QString::fromUtf8("El registro número %1 de la cadena no se puede leer.").arg(checked));

        const QString number = r.get(r.cancellation ? QStringLiteral("IDFactura/NumSerieFacturaAnulada")
                                                     : QStringLiteral("IDFactura/NumSerieFactura"));
        const QString storedHash = storedHashOf(r);
        if (storedHash.isEmpty() || storedHash.compare(recomputedHash(r), Qt::CaseInsensitive) != 0)
            return fail(i, checked, QString::fromUtf8("La huella del registro de la factura %1 no coincide con sus datos.").arg(number));

        // Each issuer is its own chain. A link to a huella this database does not hold
        // continues a record that is only at AEAT (a restored or older copy): counted, not
        // an alteration. A link to another stored record, or a restart, breaks the chain.
        const QString issuer = issuerOf(r);
        const bool first = r.get(QStringLiteral("Encadenamiento/PrimerRegistro")) == QLatin1String("S");
        const QString linked = r.get(QStringLiteral("RegistroAnterior/Huella")).toUpper();
        const bool hasPrevious = lastHash.contains(issuer);
        const bool anchored = anchors && anchors->contains(linked);
        bool linkOk;
        if (first)
            linkOk = !hasPrevious && linked.isEmpty();
        else if (hasPrevious && linked == lastHash.value(issuer))
            linkOk = true;
        else if (!linked.isEmpty() && !knownHashes.value(issuer).contains(linked)) {
            linkOk = !anchors || anchored;   // a continuation of a record only AEAT holds
            if (linkOk) {
                ++out.continuations;
                out.continuationHashes << linked;
            }
        } else
            linkOk = anchored;               // the head a sync moved back to
        if (!linkOk)
            return fail(i, checked, QString::fromUtf8("El registro de la factura %1 no enlaza con el anterior.").arg(number));

        lastHash.insert(issuer, storedHash.toUpper());
    }
    out.checked = recordXmls.size();
    return out;
}

Result verifyTail(const QStringList &recordXmls, const QString &issuerNif, const QString &headHash,
                  const QSet<QString> &anchors)
{
    Result out;
    const QString head = headHash.trimmed().toUpper();
    if (head.isEmpty() || anchors.contains(head))
        return out;
    QString tail;
    for (const QString &xml : recordXmls) {
        const ParsedRecord r = parse(xml);
        if (r.valid && issuerOf(r).compare(issuerNif, Qt::CaseInsensitive) == 0)
            tail = storedHashOf(r).toUpper();
    }
    if (tail != head) {
        out.ok = false;
        out.failedIndex = recordXmls.size();
        out.problem = QString::fromUtf8("Falta el último registro de la cadena: la base de datos no guarda el registro "
                                        "con el que enlazará el siguiente.");
    }
    return out;
}

} // namespace AeatChainCheck
