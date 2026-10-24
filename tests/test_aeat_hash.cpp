// AeatHash: the record hash (huella) and the QR verification URL a direct AEAT
// client computes itself. The expected values are the official AEAT examples:
// huella specification v0.1.2 (27-08-2024) sections 6.1-6.3, QR specification
// v0.5.0 (10-12-2025) sections 4 and 8.

#include <QtTest>

#include "aeathash.h"

using namespace AeatHash;

class TestAeatHash : public QObject
{
    Q_OBJECT

private:
    static RegistrationFields firstRecord()
    {
        RegistrationFields f;
        f.issuerNif     = "89890001K";
        f.invoiceNumber = "12345678/G33";
        f.issueDate     = QDate(2024, 1, 1);
        f.invoiceType   = "F1";
        f.totalTax      = "12.35";
        f.totalAmount   = "123.45";
        f.previousHash  = "";
        f.generatedAt   = "2024-01-01T19:20:30+01:00";
        return f;
    }

private slots:
    // Official case 1: the first record of the chain writes an empty "Huella=".
    void test_firstRegistration_officialVector()
    {
        QCOMPARE(registrationHashInput(firstRecord()),
                 QStringLiteral("IDEmisorFactura=89890001K&NumSerieFactura=12345678/G33&FechaExpedicionFactura=01-01-2024"
                                "&TipoFactura=F1&CuotaTotal=12.35&ImporteTotal=123.45&Huella="
                                "&FechaHoraHusoGenRegistro=2024-01-01T19:20:30+01:00"));
        QCOMPARE(registrationHash(firstRecord()),
                 QStringLiteral("3C464DAF61ACB827C65FDA19F352A4E3BDC2C640E9E9FC4CC058073F38F12F60"));
    }

    // Official case 2: the next registration chains the previous hash.
    void test_chainedRegistration_officialVector()
    {
        RegistrationFields f = firstRecord();
        f.invoiceNumber = "12345679/G34";
        f.previousHash  = registrationHash(firstRecord());
        f.generatedAt   = "2024-01-01T19:20:35+01:00";
        QCOMPARE(registrationHash(f), QStringLiteral("F7B94CFD8924EDFF273501B01EE5153E4CE8F259766F88CF6ACB8935802A2B97"));
    }

    // Official case 3: a cancellation chains like a registration, with its own fields.
    void test_chainedCancellation_officialVector()
    {
        CancellationFields c;
        c.issuerNif     = "89890001K";
        c.invoiceNumber = "12345679/G34";
        c.issueDate     = QDate(2024, 1, 1);
        c.previousHash  = "F7B94CFD8924EDFF273501B01EE5153E4CE8F259766F88CF6ACB8935802A2B97";
        c.generatedAt   = "2024-01-01T19:20:40+01:00";
        QCOMPARE(cancellationHashInput(c),
                 QStringLiteral("IDEmisorFacturaAnulada=89890001K&NumSerieFacturaAnulada=12345679/G34"
                                "&FechaExpedicionFacturaAnulada=01-01-2024"
                                "&Huella=F7B94CFD8924EDFF273501B01EE5153E4CE8F259766F88CF6ACB8935802A2B97"
                                "&FechaHoraHusoGenRegistro=2024-01-01T19:20:40+01:00"));
        QCOMPARE(cancellationHash(c), QStringLiteral("177547C0D57AC74748561D054A9CEC14B4C4EA23D1BEFD6F2E69E3A388F90C68"));
    }

    // Values are trimmed at both ends (inner spaces kept), so stray spaces never change the hash.
    void test_valuesTrimmed()
    {
        RegistrationFields f = firstRecord();
        f.issuerNif     = "  89890001K ";
        f.invoiceNumber = " 12345678/G33";
        QCOMPARE(registrationHash(f), registrationHash(firstRecord()));
    }

    // Amounts are written with exactly two decimals and a dot: 123.4 and 123.40 differ in the hash.
    void test_amountText()
    {
        QCOMPARE(amountText(123.4), QStringLiteral("123.40"));
        QCOMPARE(amountText(7), QStringLiteral("7.00"));
        QCOMPARE(amountText(28.405), QStringLiteral("28.41"));     // stored as 28.40499..., rounded like the books
        QCOMPARE(amountText(-12.345), QStringLiteral("-12.35"));
        QCOMPARE(amountText(0), QStringLiteral("0.00"));
    }

    // The generation time carries its offset, never "Z" (Canarias is +00:00 in winter).
    void test_timestampText()
    {
        const QDate d(2024, 1, 1);
        const QTime t(19, 20, 30);
        QCOMPARE(timestampText(QDateTime(d, t, Qt::OffsetFromUTC, 3600)), QStringLiteral("2024-01-01T19:20:30+01:00"));
        QCOMPARE(timestampText(QDateTime(d, t, Qt::OffsetFromUTC, 7200)), QStringLiteral("2024-01-01T19:20:30+02:00"));
        QCOMPARE(timestampText(QDateTime(d, t, Qt::OffsetFromUTC, 0)), QStringLiteral("2024-01-01T19:20:30+00:00"));
        QCOMPARE(timestampText(QDateTime(d, t, Qt::OffsetFromUTC, -16200)), QStringLiteral("2024-01-01T19:20:30-04:30"));
    }

    // Official QR examples: '&' in the number is percent-encoded; test and production hosts.
    void test_qrValidationUrl_officialExamples()
    {
        QCOMPARE(qrValidationUrl("89890001K", "12345678&G33", QDate(2024, 1, 1), "241.4", true),
                 QStringLiteral("https://prewww2.aeat.es/wlpl/TIKE-CONT/ValidarQR?nif=89890001K&numserie=12345678%26G33"
                                "&fecha=01-01-2024&importe=241.4"));
        QCOMPARE(qrValidationUrl("89890001K", "12345678-G33", QDate(2024, 9, 1), "241.4", false),
                 QStringLiteral("https://www2.agenciatributaria.gob.es/wlpl/TIKE-CONT/ValidarQR?nif=89890001K"
                                "&numserie=12345678-G33&fecha=01-09-2024&importe=241.4"));
    }
};

QTEST_GUILESS_MAIN(TestAeatHash)
#include "test_aeat_hash.moc"
