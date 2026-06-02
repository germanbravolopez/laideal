// Tests for the pure Verifactu model classes: VerifactuConfig (validation +
// environment URLs), VerifactuTaxItem (JSON + operation-type mapping),
// VerifactuInvoice (JSON serialization, totals, validation, rectificativa
// metadata), and the verifactu_estado string <-> enum round-trip.

#include <QtTest>
#include <QDate>
#include <QJsonArray>
#include <QJsonObject>

#include "verifactuconfig.h"
#include "verifactuinvoice.h"
#include "verifactutaxitem.h"
#include "verifactutypes.h"

class TestVerifactuModels : public QObject
{
    Q_OBJECT

private:
    static VerifactuTaxItem vatItem(double rate, double base, double amount)
    {
        VerifactuTaxItem t;
        t.setTaxRate(rate);
        t.setTaxBase(base);
        t.setTaxAmount(amount);
        return t;
    }

private slots:
    // ---- VerifactuConfig ----------------------------------------------------
    void test_configDefaultsAndValidation()
    {
        VerifactuConfig c;
        QCOMPARE(c.getEnvironment(), VerifactuConfig::TESTING);
        QCOMPARE(c.getSystemName(), QStringLiteral("LAIDEAL"));

        QVERIFY(!c.isValid()); // no service key
        QVERIFY(c.getValidationError().contains("Clave de servicio"));

        c.setServiceKey("KEY");
        QVERIFY(!c.isValid());
        QVERIFY(c.getValidationError().contains("NIF"));

        c.setEmitterData("B12345678", "");
        QVERIFY(!c.isValid());
        QVERIFY(c.getValidationError().contains("Nombre"));

        c.setEmitterData("B12345678", "La Ideal");
        QVERIFY(c.isValid());
    }

    void test_configEnvironmentUrls()
    {
        VerifactuConfig c;
        c.setEnvironment(VerifactuConfig::TESTING);
        QVERIFY(c.getValidationUrl().contains("prewww2"));   // AEAT pre-production
        QVERIFY(c.getQrUrl().endsWith("/GetQrCode"));

        c.setEnvironment(VerifactuConfig::PRODUCTION);
        QVERIFY(c.getValidationUrl().contains("www2.aeat"));
        QVERIFY(!c.getValidationUrl().contains("prewww2"));
    }

    // ---- VerifactuTaxItem ---------------------------------------------------
    void test_taxItemOperationTypeStrings()
    {
        QCOMPARE(VerifactuTaxItem::operationTypeToString(VerifactuTaxItem::S1), QStringLiteral("S1"));
        QCOMPARE(VerifactuTaxItem::operationTypeToString(VerifactuTaxItem::S2), QStringLiteral("S2"));
        QCOMPARE(VerifactuTaxItem::operationTypeToString(VerifactuTaxItem::N1), QStringLiteral("N1"));
        QCOMPARE(VerifactuTaxItem::operationTypeToString(VerifactuTaxItem::N2), QStringLiteral("N2"));
        QCOMPARE(VerifactuTaxItem::operationTypeToString(VerifactuTaxItem::EXEMPT), QStringLiteral("E1"));
    }

    void test_taxItemJsonAndValidity()
    {
        const QJsonObject j = vatItem(21.0, 100.0, 21.0).toJson();
        QCOMPARE(j.value("TaxRate").toDouble(), 21.0);
        QCOMPARE(j.value("TaxBase").toDouble(), 100.0);
        QCOMPARE(j.value("TaxAmount").toDouble(), 21.0);
        QCOMPARE(j.value("TaxType").toString(), QStringLiteral("S1")); // default operation type

        QVERIFY(vatItem(21.0, 100.0, 21.0).isValid());
        VerifactuTaxItem neg = vatItem(-5.0, 100.0, 0.0);
        QVERIFY(!neg.isValid());
        QVERIFY(neg.getValidationError().contains("negativo"));
    }

    // ---- VerifactuInvoice ---------------------------------------------------
    void test_invoiceTypeHelpers()
    {
        QVERIFY(VerifactuInvoice::isRectificationInvoiceType(VerifactuInvoice::RECTIFICATION_R1));
        QVERIFY(VerifactuInvoice::isRectificationInvoiceType(VerifactuInvoice::RECTIFICATION_R5));
        QVERIFY(!VerifactuInvoice::isRectificationInvoiceType(VerifactuInvoice::NORMAL));
        QVERIFY(!VerifactuInvoice::isRectificationInvoiceType(VerifactuInvoice::SIMPLIFIED));

        QCOMPARE(VerifactuInvoice::rectificationTypeToString(VerifactuInvoice::BY_SUBSTITUTION), QStringLiteral("S"));
        QCOMPARE(VerifactuInvoice::rectificationTypeToString(VerifactuInvoice::BY_DIFFERENCES), QStringLiteral("I"));
    }

    void test_invoiceTotalsAndJson()
    {
        VerifactuInvoice inv;
        inv.setInvoiceNumber("3245");
        inv.setInvoiceDate(QDate(2026, 6, 2));
        inv.setInvoiceType(VerifactuInvoice::SIMPLIFIED);
        inv.setSellerNIF("B12345678");
        inv.setSellerName("La Ideal");
        inv.addTaxItem(vatItem(21.0, 100.0, 21.0));
        inv.addTaxItem(vatItem(10.0, 50.0, 5.0));
        inv.calculateTotals();

        const QJsonObject j = inv.toJson();
        QCOMPARE(j.value("InvoiceID").toString(), QStringLiteral("3245"));
        QCOMPARE(j.value("InvoiceDate").toString(), QStringLiteral("2026-06-02"));
        QCOMPARE(j.value("InvoiceType").toString(), QStringLiteral("F2")); // simplified
        QCOMPARE(j.value("SellerID").toString(), QStringLiteral("B12345678"));
        QCOMPARE(j.value("CompanyName").toString(), QStringLiteral("La Ideal"));
        QCOMPARE(j.value("TaxItems").toArray().size(), 2);
        // totalTaxAmount = 21 + 5 = 26; totalAmount = (100 + 50) + 26 = 176
        QCOMPARE(j.value("TotalTaxAmount").toDouble(), 26.0);
        QCOMPARE(j.value("TotalAmount").toDouble(), 176.0);
    }

    void test_invoiceValidation()
    {
        VerifactuInvoice empty;
        QVERIFY(!empty.isValid());
        QVERIFY(empty.getValidationError().contains("Número de factura"));

        // SIMPLIFIED (F2) needs no buyer.
        VerifactuInvoice f2;
        f2.setInvoiceNumber("3245");
        f2.setInvoiceDate(QDate(2026, 6, 2));
        f2.setInvoiceType(VerifactuInvoice::SIMPLIFIED);
        f2.setSellerNIF("B12345678");
        f2.setSellerName("La Ideal");
        f2.addTaxItem(vatItem(21.0, 100.0, 21.0));
        QVERIFY2(f2.isValid(), qPrintable(f2.getValidationError()));

        // NORMAL (F1) requires a buyer.
        VerifactuInvoice f1;
        f1.setInvoiceNumber("3246");
        f1.setInvoiceDate(QDate(2026, 6, 2));
        f1.setInvoiceType(VerifactuInvoice::NORMAL);
        f1.setSellerNIF("B12345678");
        f1.setSellerName("La Ideal");
        f1.addTaxItem(vatItem(21.0, 100.0, 21.0));
        QVERIFY(!f1.isValid());
        QVERIFY(f1.getValidationError().contains("comprador"));
    }

    void test_invoiceRectificationJson()
    {
        VerifactuInvoice inv;
        inv.setInvoiceNumber("3247");
        inv.setInvoiceDate(QDate(2026, 6, 2));
        inv.setInvoiceType(VerifactuInvoice::RECTIFICATION_R1);
        inv.setSellerNIF("B12345678");
        inv.setSellerName("La Ideal");
        inv.setRectificationType(VerifactuInvoice::BY_SUBSTITUTION);
        inv.setRectificationTaxBase(100.0);
        inv.setRectificationTaxAmount(21.0);
        inv.addTaxItem(vatItem(21.0, 80.0, 16.8));

        const QJsonObject j = inv.toJson();
        QCOMPARE(j.value("InvoiceType").toString(), QStringLiteral("R1"));
        QCOMPARE(j.value("RectificationType").toString(), QStringLiteral("S"));
        QCOMPARE(j.value("RectificationTaxBase").toDouble(), 100.0);
        QCOMPARE(j.value("RectificationTaxAmount").toDouble(), 21.0);
    }

    // ---- verifactu_estado round-trip ---------------------------------------
    void test_estadoRoundTrip()
    {
        const VerifactuEstado all[] = {
            VerifactuEstado::NotSubmitted, VerifactuEstado::Enviada,
            VerifactuEstado::Anulada, VerifactuEstado::Rectificada, VerifactuEstado::Error
        };
        for (VerifactuEstado e : all)
            QVERIFY(verifactuEstadoFromString(verifactuEstadoToString(e)) == e);

        QCOMPARE(verifactuEstadoToString(VerifactuEstado::Enviada), QStringLiteral("ENVIADA"));
        // PENDIENTE / empty / unknown all map to NotSubmitted.
        QVERIFY(verifactuEstadoFromString("PENDIENTE") == VerifactuEstado::NotSubmitted);
        QVERIFY(verifactuEstadoFromString("") == VerifactuEstado::NotSubmitted);
        QVERIFY(verifactuEstadoFromString("garbage") == VerifactuEstado::NotSubmitted);
    }
};

QTEST_GUILESS_MAIN(TestVerifactuModels)
#include "test_verifactu_models.moc"
