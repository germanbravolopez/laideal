#ifndef VERIFACTUINVOICE_H
#define VERIFACTUINVOICE_H

#include <QString>
#include <QList>
#include <QDate>
#include <QJsonObject>

// Represents a single tax line (IVA, IGIC, etc.) within a Verifactu invoice.
class VerifactuTaxItem
{
public:
    enum TaxType {
        VAT,    // IVA (standard)
        IGIC,   // Canary Islands tax
        OTHER
    };

    enum OperationType {
        S1,     // subject to tax (standard)
        S2,     // reverse charge
        N1,     // not subject (art. 7, 14)
        N2,     // not subject (location rules)
        EXEMPT
    };

    VerifactuTaxItem();

    void setTaxType(TaxType type) { m_taxType = type; }
    void setTaxRate(double rate) { m_taxRate = rate; }
    void setTaxBase(double base) { m_taxBase = base; }
    void setTaxAmount(double amount) { m_taxAmount = amount; }
    void setOperationType(OperationType type) { m_operationType = type; }
    void setDescription(const QString &desc) { m_description = desc; }

    TaxType getTaxType() const { return m_taxType; }
    double getTaxRate() const { return m_taxRate; }
    double getTaxBase() const { return m_taxBase; }
    double getTaxAmount() const { return m_taxAmount; }
    OperationType getOperationType() const { return m_operationType; }
    QString getDescription() const { return m_description; }

    QJsonObject toJson() const;
    bool isValid() const;
    QString getValidationError() const;

    static QString operationTypeToString(OperationType type);

private:
    TaxType m_taxType;
    double m_taxRate;
    double m_taxBase;
    double m_taxAmount;
    OperationType m_operationType;
    QString m_description;
    mutable QString m_validationError;
};

// Represents a complete Verifactu invoice for submission to the AEAT.
class VerifactuInvoice
{
public:
    enum InvoiceType {
        NORMAL,         // F1 - standard invoice
        SIMPLIFIED,     // F2 - simplified invoice
        SUBSTITUTE,     // F3 - substitute for simplified
        RECTIFICATION   // R1 - corrective invoice
    };

    VerifactuInvoice();
    ~VerifactuInvoice();

    void setInvoiceNumber(const QString &number) { m_invoiceNumber = number; }
    void setInvoiceDate(const QDate &date) { m_invoiceDate = date; }
    void setInvoiceType(InvoiceType type) { m_invoiceType = type; }
    void setDescription(const QString &desc) { m_description = desc; }

    void setSellerNIF(const QString &nif) { m_sellerNIF = nif; }
    void setSellerName(const QString &name) { m_sellerName = name; }

    void setBuyerNIF(const QString &nif) { m_buyerNIF = nif; }
    void setBuyerName(const QString &name) { m_buyerName = name; }

    void setTotalAmount(double amount) { m_totalAmount = amount; }
    void setTotalTaxAmount(double amount) { m_totalTaxAmount = amount; }

    QString getInvoiceNumber() const { return m_invoiceNumber; }
    QDate getInvoiceDate() const { return m_invoiceDate; }
    InvoiceType getInvoiceType() const { return m_invoiceType; }
    QString getDescription() const { return m_description; }

    QString getSellerNIF() const { return m_sellerNIF; }
    QString getSellerName() const { return m_sellerName; }

    QString getBuyerNIF() const { return m_buyerNIF; }
    QString getBuyerName() const { return m_buyerName; }

    double getTotalAmount() const { return m_totalAmount; }
    double getTotalTaxAmount() const { return m_totalTaxAmount; }

    void addTaxItem(const VerifactuTaxItem &item) { m_taxItems.append(item); }
    const QList<VerifactuTaxItem> &getTaxItems() const { return m_taxItems; }
    void clearTaxItems() { m_taxItems.clear(); }

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

    mutable QString m_validationError;

    QString invoiceTypeToString(InvoiceType type) const;
    QString operationTypeToString(VerifactuTaxItem::OperationType type) const;
    QString taxTypeToString(VerifactuTaxItem::TaxType type) const;
};

#endif // VERIFACTUINVOICE_H
