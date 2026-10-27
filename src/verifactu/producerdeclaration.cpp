#include "producerdeclaration.h"
#include "aeatrecord.h"
#include "appsettings.h"

#include <QStringList>

namespace ProducerDeclaration {

namespace {

QString pending()
{
    return QString::fromUtf8("<i>(pendiente)</i>");
}

QString item(const QString &letter, const QString &description, const QString &value)
{
    return QStringLiteral("<p><b>%1) %2:</b> %3</p>").arg(letter, description, value);
}

const char *const kMonths[] = { "enero", "febrero", "marzo", "abril", "mayo", "junio", "julio",
                                "agosto", "septiembre", "octubre", "noviembre", "diciembre" };

} // namespace

Signature currentSignature()
{
    // j) the shop's address from the settings (never in the source); the date is the day
    // the producer signed the version that ships (12.0), set again for each such release.
    const AppSettings *s = AppSettings::instance();
    QStringList lines;
    for (const QString &line : { s->businessAddress().trimmed(), s->businessCity().trimmed() })
        if (!line.isEmpty())
            lines << line;
    const QString address = lines.join(QStringLiteral(", "));
    return { address.isEmpty() ? QString() : address + QString::fromUtf8(", España"), QDate(2026, 10, 27),
             QString::fromUtf8("Granada, España") };
}

bool isComplete(const Signature &signature)
{
    return !signature.postalAddress.trimmed().isEmpty() && signature.date.isValid()
           && !signature.place.trimmed().isEmpty();
}

QString html(const QString &version, const Signature &signature)
{
    const AeatRecord::SystemInfo system = AeatRecord::laIdealSystem(version, QString());
    const QString producer = system.producerName.toHtmlEscaped();
    const QString date = signature.date.isValid()
        ? QStringLiteral("%1 de %2 de %3").arg(signature.date.day())
              .arg(QString::fromLatin1(kMonths[signature.date.month() - 1])).arg(signature.date.year())
        : pending();
    const QString place = signature.place.trimmed().isEmpty() ? pending() : signature.place.toHtmlEscaped();

    QStringList out;
    out << QString::fromUtf8("<h3 align=\"center\">DECLARACIÓN RESPONSABLE DEL SISTEMA INFORMÁTICO DE FACTURACIÓN</h3>")
        << item("a", QString::fromUtf8("Nombre del sistema informático"), system.systemName.toHtmlEscaped())
        << item("b", QString::fromUtf8("Código identificador del sistema informático"), system.systemId)
        << item("c", QString::fromUtf8("Identificador completo de la versión"), version.toHtmlEscaped())
        << item("d", QString::fromUtf8("Componentes, hardware y software, y descripción de lo que hace y de sus principales funcionalidades"),
                QString::fromUtf8("programa La Ideal para Windows (C++ con Qt, base de datos SQLite local), "
                                  "ordenador personal con Windows, impresora térmica de tickets y certificado "
                                  "electrónico del obligado tributario. Gestiona la recepción y la recogida de "
                                  "prendas de una tintorería; expide facturas simplificadas y rectificativas; "
                                  "genera los registros de facturación de alta y de anulación con su huella "
                                  "encadenada, los remite directamente al servicio web de la AEAT y los conserva; "
                                  "imprime el código QR y permite exportar los registros."))
        << item("e", QString::fromUtf8("Solo puede funcionar exclusivamente como «VERI*FACTU»"), QString::fromUtf8("Sí"))
        << item("f", QString::fromUtf8("Permite ser usado por varios obligados tributarios o por un mismo usuario para varios obligados tributarios"),
                QStringLiteral("No"))
        << item("g", QString::fromUtf8("Tipos de firma utilizados para firmar los registros de facturación y de evento"),
                QString::fromUtf8("No aplica: el sistema funciona exclusivamente como «VERI*FACTU»"))
        << item("h", QString::fromUtf8("Nombre y apellidos de la persona productora del sistema informático"), producer)
        << item("i", QString::fromUtf8("NIF de la persona productora del sistema informático"), system.producerNif)
        << item("j", QString::fromUtf8("Dirección postal completa de contacto de la persona productora"),
                signature.postalAddress.trimmed().isEmpty() ? pending() : signature.postalAddress.toHtmlEscaped())
        << item("k", QString::fromUtf8("Declaración de cumplimiento"),
                QString::fromUtf8("%1 hace constar que el sistema informático La Ideal, en la versión %2, cumple con "
                                  "lo dispuesto en el artículo 29.2.j) de la Ley 58/2003, de 17 de diciembre, General "
                                  "Tributaria, en el Reglamento que establece los requisitos que deben adoptar los "
                                  "sistemas y programas informáticos o electrónicos que soporten los procesos de "
                                  "facturación de empresarios y profesionales, y la estandarización de formatos de los "
                                  "registros de facturación, aprobado por el Real Decreto 1007/2023, de 5 de diciembre, "
                                  "en la Orden HAC/1177/2024, de 17 de octubre, y en la sede electrónica de la Agencia "
                                  "Estatal de Administración Tributaria para todo aquello que complete las "
                                  "especificaciones de dicha orden.").arg(producer, version.toHtmlEscaped()))
        << item("l", QString::fromUtf8("Fecha y lugar en los que se suscribe la declaración responsable"),
                QStringLiteral("%1, %2").arg(date, place))
        << QString::fromUtf8("<h4>Anexo</h4>")
        << QString::fromUtf8("<p>Uso en una instalación con un único operador, identificado por la sesión de Windows del "
                             "puesto en el que se ejecuta: bajo este alcance se cumple la trazabilidad por usuario del "
                             "artículo 8.1 del Real Decreto 1007/2023. Un segundo operador exigirá identificar a cada "
                             "uno en cada evento de facturación.</p>");
    return out.join(QString());
}

} // namespace ProducerDeclaration
