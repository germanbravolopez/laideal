#ifndef VERIFACTUINTEGRATION_H
#define VERIFACTUINTEGRATION_H

#include <QString>
#include <QDate>
#include "verifactumanager.h"

// High-level facade over VerifactuManager: create/submit/cancel invoices and generate QR codes.
class VerifactuIntegration : public QObject
{
    Q_OBJECT

public:
    explicit VerifactuIntegration(QObject *parent = nullptr);
    ~VerifactuIntegration();

    bool initialize();

    VerifactuManager *getManager() const { return m_manager; }

    VerifactuResult submitSimplifiedInvoice(
        const QString &invoiceNumber,
        const QDate &invoiceDate,
        double taxBase,
        double taxRate,
        const QString &description = QString()
    );

    VerifactuResult createAndSubmitInvoice(
        const QString &invoiceNumber,
        const QDate &invoiceDate,
        const QString &buyerNIF,
        const QString &buyerName,
        double taxBase,
        double taxRate,
        const QString &description = QString()
    );

    VerifactuResult cancelInvoice(
        const QString &invoiceNumber,
        const QDate &invoiceDate
    );

    VerifactuResult generateQR(
        const QString &invoiceNumber,
        const QDate &invoiceDate,
        double taxBase,
        double taxRate,
        const QString &description = QString()
    );

    QString getConfigInfo() const;
    bool isConfigured() const;
    QString getLastError() const { return m_lastError; }

private:
    VerifactuManager *m_manager;
    QString m_lastError;

    bool validateEmitterConfiguration();
    bool loadEmitterConfiguration();
};

#endif // VERIFACTUINTEGRATION_H
