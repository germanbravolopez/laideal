// AeatRecord: the registration / cancellation records and the SOAP envelopes of the
// direct AEAT client. Checks the content and order of what is written; the files it
// writes into $AEAT_XML_OUT are then validated against the official AEAT schemas
// by tests/aeat/validate_aeat_xml.py (ctest entry aeat_xsd).

#include <QtTest>
#include <QXmlStreamReader>

#include "aeathash.h"
#include "aeatrecord.h"

using namespace AeatRecord;

class TestAeatRecord : public QObject
{
    Q_OBJECT

private:
    static QDateTime at(int h, int m, int s) { return QDateTime(QDate(2026, 10, 10), QTime(h, m, s), Qt::OffsetFromUTC, 7200); }

    static SystemInfo system()
    {
        return { "Germán Bravo López", "12345678Z", "LAIDEAL", "LI", "11.0", "LAIDEAL-0001" };
    }

    static Registration ticket(const QString &number, double base, const PreviousRecord &previous = {})
    {
        Registration r;
        r.invoice     = { "89890001K", number, QDate(2026, 10, 10) };
        r.issuerName  = "Tintorería La Ideal";
        r.invoiceType = "F2";
        r.description = "Servicios de lavandería";
        r.taxLines    = { { 21.0, base, base * 0.21 } };
        r.previous    = previous;
        r.generatedAt = at(10, 0, 0);
        return r;
    }

    // Element local names in document order, and the text of each leaf.
    static QStringList names(const QString &xml, QHash<QString, QString> *texts = nullptr)
    {
        QStringList out;
        QXmlStreamReader r(xml);
        QString current;
        while (!r.atEnd()) {
            r.readNext();
            if (r.isStartElement()) {
                current = r.name().toString();
                out << current;
            } else if (r.isCharacters() && !r.isWhitespace() && texts && !texts->contains(current)) {
                texts->insert(current, r.text().toString());
            }
        }
        if (r.hasError())
            qWarning() << "XML error:" << r.errorString();
        return out;
    }

    static void writeSample(const QString &name, const QString &xml)
    {
        const QString dir = qEnvironmentVariable("AEAT_XML_OUT");
        if (dir.isEmpty())
            return;
        QDir().mkpath(dir);
        QFile f(dir + "/" + name);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(xml.toUtf8());
    }

private slots:
    // A full record reproduces the official AEAT hash (case 1) from its amounts.
    void test_recordHash_matchesOfficialVector()
    {
        Registration r;
        r.invoice     = { "89890001K", "12345678/G33", QDate(2024, 1, 1) };
        r.invoiceType = "F1";
        r.taxLines    = { { 21.0, 111.10, 12.35 } };
        r.generatedAt = QDateTime(QDate(2024, 1, 1), QTime(19, 20, 30), Qt::OffsetFromUTC, 3600);
        QCOMPARE(hashOf(r), QStringLiteral("3C464DAF61ACB827C65FDA19F352A4E3BDC2C640E9E9FC4CC058073F38F12F60"));
    }

    // A simplified ticket: the schema's element order, the amounts in cents, the first record of the chain.
    void test_simplifiedTicket_firstRecord()
    {
        const Registration r = ticket("30837", 24.79);
        QHash<QString, QString> t;
        const QStringList n = names(registrationXml(r, system()), &t);
        QCOMPARE(n.mid(0, 7), (QStringList{ "RegistroAlta", "IDVersion", "IDFactura", "IDEmisorFactura",
                                            "NumSerieFactura", "FechaExpedicionFactura", "NombreRazonEmisor" }));
        QCOMPARE(n.indexOf("TipoFactura") + 1, n.indexOf("DescripcionOperacion"));
        QVERIFY(n.indexOf("Desglose") < n.indexOf("CuotaTotal"));
        QVERIFY(n.indexOf("Encadenamiento") < n.indexOf("SistemaInformatico"));
        QCOMPARE(n.last(), QStringLiteral("Huella"));
        QVERIFY(n.contains("PrimerRegistro"));
        QVERIFY(!n.contains("RegistroAnterior") && !n.contains("Subsanacion") && !n.contains("TipoRectificativa"));
        QCOMPARE(t.value("FechaExpedicionFactura"), QStringLiteral("10-10-2026"));
        QCOMPARE(t.value("TipoImpositivo"), QStringLiteral("21.00"));
        QCOMPARE(t.value("BaseImponibleOimporteNoSujeto"), QStringLiteral("24.79"));
        QCOMPARE(t.value("CuotaRepercutida"), QStringLiteral("5.21"));
        QCOMPARE(t.value("CuotaTotal"), QStringLiteral("5.21"));
        QCOMPARE(t.value("ImporteTotal"), QStringLiteral("30.00"));
        QCOMPARE(t.value("PrimerRegistro"), QStringLiteral("S"));
        QCOMPARE(t.value("FechaHoraHusoGenRegistro"), QStringLiteral("2026-10-10T10:00:00+02:00"));
        QCOMPARE(t.value("TipoHuella"), QStringLiteral("01"));
        QCOMPARE(t.value("Huella"), hashOf(r));
        QCOMPARE(t.value("TipoUsoPosibleSoloVerifactu"), QStringLiteral("S"));
        writeSample("alta_f2_first.xml", submissionEnvelope(r.issuerName, r.invoice.issuerNif, { registrationXml(r, system()) }));
    }

    // The hash in the record is the one recomputed from the values the record shows.
    void test_hashAgreesWithWrittenValues()
    {
        const Registration r = ticket("30838", 10.0 / 1.21);   // 8.264..., written 8.26 + 1.74
        QHash<QString, QString> t;
        names(registrationXml(r, system()), &t);
        AeatHash::RegistrationFields f;
        f.issuerNif = t.value("IDEmisorFactura");
        f.invoiceNumber = t.value("NumSerieFactura");
        f.issueDate = QDate::fromString(t.value("FechaExpedicionFactura"), "dd-MM-yyyy");
        f.invoiceType = t.value("TipoFactura");
        f.totalTax = t.value("CuotaTotal");
        f.totalAmount = t.value("ImporteTotal");
        f.previousHash = QString();
        f.generatedAt = t.value("FechaHoraHusoGenRegistro");
        QCOMPARE(AeatHash::registrationHash(f), t.value("Huella"));
        QCOMPARE(t.value("ImporteTotal"), QStringLiteral("10.00"));
    }

    // The next record chains the previous one (issuer, number, date and hash).
    void test_chainedRecord()
    {
        const Registration first = ticket("30837", 24.79);
        const Registration second = ticket("30838", 8.26, { first.invoice, hashOf(first) });
        QHash<QString, QString> t;
        const QString xml = registrationXml(second, system());
        const QStringList n = names(xml, &t);
        QVERIFY(n.contains("RegistroAnterior") && !n.contains("PrimerRegistro"));
        QVERIFY(xml.contains("<sf:RegistroAnterior><sf:IDEmisorFactura>89890001K</sf:IDEmisorFactura>"
                             "<sf:NumSerieFactura>30837</sf:NumSerieFactura>"
                             "<sf:FechaExpedicionFactura>10-10-2026</sf:FechaExpedicionFactura>"
                             "<sf:Huella>" + hashOf(first) + "</sf:Huella></sf:RegistroAnterior>"));
        QVERIFY(hashOf(second) != hashOf(ticket("30838", 8.26)));
        writeSample("alta_f2_chained.xml", submissionEnvelope(second.issuerName, "89890001K",
                                                              { registrationXml(first, system()), xml }));
    }

    // Rectificativa R5 by substitution: the rectified invoice and its former amounts.
    void test_rectificationBySubstitution()
    {
        Registration r = ticket("30900", 16.53, { { "89890001K", "30838", QDate(2026, 10, 10) }, "AB12" });
        r.invoiceType = "R5";
        r.rectificationType = "S";
        r.rectifiedInvoices = { { "89890001K", "30837", QDate(2026, 10, 9) } };
        r.hasRectifiedAmounts = true;
        r.rectifiedBase = 24.79;
        r.rectifiedTax = 5.21;
        QHash<QString, QString> t;
        const QStringList n = names(registrationXml(r, system()), &t);
        QCOMPARE(t.value("TipoFactura"), QStringLiteral("R5"));
        QCOMPARE(t.value("TipoRectificativa"), QStringLiteral("S"));
        QVERIFY(n.indexOf("TipoRectificativa") < n.indexOf("FacturasRectificadas"));
        QVERIFY(n.indexOf("FacturasRectificadas") < n.indexOf("ImporteRectificacion"));
        QVERIFY(n.indexOf("ImporteRectificacion") < n.indexOf("DescripcionOperacion"));
        QCOMPARE(t.value("BaseRectificada"), QStringLiteral("24.79"));
        QCOMPARE(t.value("CuotaRectificada"), QStringLiteral("5.21"));
        writeSample("alta_r5_substitution.xml", submissionEnvelope(r.issuerName, "89890001K", { registrationXml(r, system()) }));
    }

    // Rectificativa R5 by differences: signed amounts, no former amounts.
    void test_rectificationByDifferences()
    {
        Registration r = ticket("30901", -4.13);
        r.invoiceType = "R5";
        r.rectificationType = "I";
        r.rectifiedInvoices = { { "89890001K", "30837", QDate(2026, 10, 9) } };
        QHash<QString, QString> t;
        const QStringList n = names(registrationXml(r, system()), &t);
        QVERIFY(!n.contains("ImporteRectificacion"));
        QCOMPARE(t.value("BaseImponibleOimporteNoSujeto"), QStringLiteral("-4.13"));
        QCOMPARE(t.value("CuotaRepercutida"), QStringLiteral("-0.87"));
        QCOMPARE(t.value("ImporteTotal"), QStringLiteral("-5.00"));
        writeSample("alta_r5_differences.xml", submissionEnvelope(r.issuerName, "89890001K", { registrationXml(r, system()) }));
    }

    // A record sent again after AEAT rejected the earlier one carries Subsanacion / RechazoPrevio.
    void test_afterRejectionFlags()
    {
        Registration r = ticket("30902", 8.26);
        r.afterRejection = true;
        QHash<QString, QString> t;
        const QStringList n = names(registrationXml(r, system()), &t);
        QCOMPARE(t.value("Subsanacion"), QStringLiteral("S"));
        QCOMPARE(t.value("RechazoPrevio"), QStringLiteral("S"));
        QCOMPARE(n.indexOf("NombreRazonEmisor") + 1, n.indexOf("Subsanacion"));
        writeSample("alta_after_rejection.xml", submissionEnvelope(r.issuerName, "89890001K", { registrationXml(r, system()) }));
    }

    // A cancellation names the cancelled invoice with its own field names and chains like an alta.
    void test_cancellation()
    {
        const Registration alta = ticket("30837", 24.79);
        Cancellation c;
        c.invoice = alta.invoice;
        c.previous = { alta.invoice, hashOf(alta) };
        c.generatedAt = at(11, 0, 0);
        QHash<QString, QString> t;
        const QString xml = cancellationXml(c, system());
        const QStringList n = names(xml, &t);
        QCOMPARE(n.mid(0, 6), (QStringList{ "RegistroAnulacion", "IDVersion", "IDFactura", "IDEmisorFacturaAnulada",
                                            "NumSerieFacturaAnulada", "FechaExpedicionFacturaAnulada" }));
        QVERIFY(xml.endsWith("<sf:TipoHuella>01</sf:TipoHuella><sf:Huella>" + hashOf(c) + "</sf:Huella></sf:RegistroAnulacion>"));
        QVERIFY(!n.contains("RechazoPrevio"));
        writeSample("anulacion.xml", submissionEnvelope(alta.issuerName, "89890001K", { xml }));
        writeSample("anulacion_record.xml", xml);
        writeSample("alta_record.xml", registrationXml(alta, system()));
    }

    // Text is escaped: an ampersand in a name or a number does not break the XML.
    void test_textEscaped()
    {
        Registration r = ticket("A&B-1", 8.26);
        r.issuerName = "Pérez & Hijos <S.L.>";
        QHash<QString, QString> t;
        names(registrationXml(r, system()), &t);
        QCOMPARE(t.value("NombreRazonEmisor"), QStringLiteral("Pérez & Hijos <S.L.>"));
        const QString envelope = submissionEnvelope(r.issuerName, "89890001K", { registrationXml(r, system()) });
        QVERIFY(envelope.contains("<sf:NombreRazon>Pérez &amp; Hijos &lt;S.L.&gt;</sf:NombreRazon>"));
        QXmlStreamReader reader(envelope);
        while (!reader.atEnd())
            reader.readNext();
        QVERIFY2(!reader.hasError(), qPrintable(reader.errorString()));
        writeSample("alta_escaped.xml", envelope);
    }

    // The query asks for one month of the issuer, optionally one invoice.
    void test_queryEnvelope()
    {
        const QString one = queryEnvelope("Tintorería La Ideal", "89890001K", 2026, 3, "30837");
        QVERIFY(one.contains("<sf:Ejercicio>2026</sf:Ejercicio><sf:Periodo>03</sf:Periodo>"));
        QVERIFY(one.contains("<sfLRC:NumSerieFactura>30837</sfLRC:NumSerieFactura>"));
        QVERIFY(!queryEnvelope("Tintorería La Ideal", "89890001K", 2026, 11).contains("NumSerieFactura"));
        writeSample("consulta.xml", one);
    }
};

QTEST_GUILESS_MAIN(TestAeatRecord)
#include "test_aeat_record.moc"
