#ifndef VERIFACTUINVOICE_H
#define VERIFACTUINVOICE_H

#include <QDate>
#include <QJsonObject>
#include <QList>
#include <QString>

#include "verifactutaxitem.h"

// Represents a complete Verifactu invoice for submission to the AEAT.
class VerifactuInvoice
{
public:
    enum InvoiceType {
        NORMAL,             // F1 - standard invoice
        SIMPLIFIED,         // F2 - simplified invoice
        SUBSTITUTE,         // F3 - substitute for simplified
        RECTIFICATION_R1,   // R1 - corrective (legal error, art. 80.1/80.2/80.6 LIVA)
        RECTIFICATION_R2,   // R2 - corrective (art. 80.3 - buyer insolvency)
        RECTIFICATION_R3,   // R3 - corrective (art. 80.4 - bad debts)
        RECTIFICATION_R4,   // R4 - corrective (other)
        RECTIFICATION_R5    // R5 - corrective on simplified invoices
    };

    // RectificationType (Verifactu API): how the rectificativa relates to the
    // original invoice. Required when InvoiceType is R1-R5.
    enum RectificationType {
        BY_DIFFERENCES,   // I - rectificativa por diferencias (delta)
        BY_SUBSTITUTION   // S - rectificativa por sustitucion (replaces original)
    };

    VerifactuInvoice();
    ~VerifactuInvoice();

    void setInvoiceNumber(const QString &number) { m_invoiceNumber = number; }
    void setInvoiceDate(const QDate &date) { m_invoiceDate = date; }
    void setInvoiceType(InvoiceType type) { m_invoiceType = type; }
    void setDescription(const QString &desc) { m_description = desc; }

    void setSellerNIF(const QString &nif) { m_sellerNIF = nif; }
    void setSellerName(const QString &name) { m_sellerName = name; }

    // Rectificativa fields - only meaningful when invoice type is R1-R5.
    // For substitution (S) these carry the ORIGINAL values being replaced.
    // For differences (I) they are not required by AEAT.
    void setRectificationType(RectificationType type) { m_rectificationType = type; m_hasRectification = true; }
    void setRectificationTaxBase(double base) { m_rectificationTaxBase = base; }
    void setRectificationTaxAmount(double amount) { m_rectificationTaxAmount = amount; }

    RectificationType getRectificationType() const { return m_rectificationType; }
    double getRectificationTaxBase() const { return m_rectificationTaxBase; }
    double getRectificationTaxAmount() const { return m_rectificationTaxAmount; }

    static QString rectificationTypeToString(RectificationType type);
    static bool isRectificationInvoiceType(InvoiceType type);

    QString getInvoiceNumber() const { return m_invoiceNumber; }
    QDate getInvoiceDate() const { return m_invoiceDate; }

    QString getSellerNIF() const { return m_sellerNIF; }
    QString getSellerName() const { return m_sellerName; }

    double getTotalAmount() const { return m_totalAmount; }

    void addTaxItem(const VerifactuTaxItem &item) { m_taxItems.append(item); }
    const QList<VerifactuTaxItem> &getTaxItems() const { return m_taxItems; }

    QJsonObject toJson() const;
    bool isValid() const;
    QString getValidationError() const;
    void calculateTotals();

private:
    QString m_invoiceNumber;
    QDate m_invoiceDate;
    InvoiceType m_invoiceType;
    QString m_description;

    QString m_sellerNIF;
    QString m_sellerName;

    QString m_buyerNIF;
    QString m_buyerName;

    QList<VerifactuTaxItem> m_taxItems;

    double m_totalAmount;
    double m_totalTaxAmount;

    bool m_hasRectification = false;
    RectificationType m_rectificationType = BY_DIFFERENCES;
    double m_rectificationTaxBase = 0.0;
    double m_rectificationTaxAmount = 0.0;

    mutable QString m_validationError;

    QString invoiceTypeToString(InvoiceType type) const;
};

#endif // VERIFACTUINVOICE_H
