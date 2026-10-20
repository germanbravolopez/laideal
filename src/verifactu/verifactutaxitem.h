#ifndef VERIFACTUTAXITEM_H
#define VERIFACTUTAXITEM_H

#include <QJsonObject>
#include <QString>

// Represents a single tax line (IVA) within a Verifactu invoice. Always operation
// type S1 (subject, not exempt): the shop has no other.
class VerifactuTaxItem
{
public:
    VerifactuTaxItem();

    void setTaxRate(double rate) { m_taxRate = rate; }
    void setTaxBase(double base) { m_taxBase = base; }
    void setTaxAmount(double amount) { m_taxAmount = amount; }

    double getTaxRate() const { return m_taxRate; }
    double getTaxBase() const { return m_taxBase; }
    double getTaxAmount() const { return m_taxAmount; }

    QJsonObject toJson() const;
    bool isValid() const;
    QString getValidationError() const;

private:
    double m_taxRate;
    double m_taxBase;
    double m_taxAmount;
    mutable QString m_validationError;
};

#endif // VERIFACTUTAXITEM_H
