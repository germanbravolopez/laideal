#ifndef VERIFACTUTAXITEM_H
#define VERIFACTUTAXITEM_H

#include <QJsonObject>
#include <QString>

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

#endif // VERIFACTUTAXITEM_H
