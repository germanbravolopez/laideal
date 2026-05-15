#ifndef VERIFACTUINVOICE_H
#define VERIFACTUINVOICE_H

#include <QString>
#include <QList>
#include <QDate>
#include <QJsonObject>

/**
 * @brief Clase que representa un ítem de impuesto en una factura Verifactu
 */
class VerifactuTaxItem
{
public:
    enum TaxType {
        VAT,            // IVA normal (Impuesto sobre el Valor Añadido)
        IGIC,           // IGIC (Canarias)
        OTHER           // Otro impuesto
    };

    enum OperationType {
        S1,             // Operación sujeta (estándar)
        S2,             // Inversión del sujeto pasivo
        N1,             // No sujeta (art. 7, 14)
        N2,             // No sujeta (reglas de localización)
        EXEMPT          // Exenta
    };

    VerifactuTaxItem();

    // Setters
    void setTaxType(TaxType type) { m_taxType = type; }
    void setTaxRate(double rate) { m_taxRate = rate; }
    void setTaxBase(double base) { m_taxBase = base; }
    void setTaxAmount(double amount) { m_taxAmount = amount; }
    void setOperationType(OperationType type) { m_operationType = type; }
    void setDescription(const QString &desc) { m_description = desc; }

    // Getters
    TaxType getTaxType() const { return m_taxType; }
    double getTaxRate() const { return m_taxRate; }
    double getTaxBase() const { return m_taxBase; }
    double getTaxAmount() const { return m_taxAmount; }
    OperationType getOperationType() const { return m_operationType; }
    QString getDescription() const { return m_description; }

    // Conversión a JSON para API REST
    QJsonObject toJson() const;

    // Validación
    bool isValid() const;
    QString getValidationError() const;

    // Funciones auxiliares estáticas
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

/**
 * @brief Clase que representa una factura Verifactu
 *
 * Contiene toda la información necesaria para crear una factura
 * compatible con el sistema Verifactu de la AEAT
 */
class VerifactuInvoice
{
public:
    enum InvoiceType {
        NORMAL,             // F1 - Factura normal
        SIMPLIFIED,         // F2 - Factura simplificada
        SUBSTITUTE,         // F3 - Sustitución de simplificada
        RECTIFICATION       // R1 - Rectificativa
    };

    VerifactuInvoice();
    ~VerifactuInvoice();

    // Setters - Datos básicos
    void setInvoiceNumber(const QString &number) { m_invoiceNumber = number; }
    void setInvoiceDate(const QDate &date) { m_invoiceDate = date; }
    void setInvoiceType(InvoiceType type) { m_invoiceType = type; }
    void setDescription(const QString &desc) { m_description = desc; }

    // Setters - Emisor
    void setSellerNIF(const QString &nif) { m_sellerNIF = nif; }
    void setSellerName(const QString &name) { m_sellerName = name; }

    // Setters - Comprador (opcional para facturas simplificadas)
    void setBuyerNIF(const QString &nif) { m_buyerNIF = nif; }
    void setBuyerName(const QString &name) { m_buyerName = name; }

    // Setters - Totales
    void setTotalAmount(double amount) { m_totalAmount = amount; }
    void setTotalTaxAmount(double amount) { m_totalTaxAmount = amount; }

    // Getters - Datos básicos
    QString getInvoiceNumber() const { return m_invoiceNumber; }
    QDate getInvoiceDate() const { return m_invoiceDate; }
    InvoiceType getInvoiceType() const { return m_invoiceType; }
    QString getDescription() const { return m_description; }

    // Getters - Emisor
    QString getSellerNIF() const { return m_sellerNIF; }
    QString getSellerName() const { return m_sellerName; }

    // Getters - Comprador
    QString getBuyerNIF() const { return m_buyerNIF; }
    QString getBuyerName() const { return m_buyerName; }

    // Getters - Totales
    double getTotalAmount() const { return m_totalAmount; }
    double getTotalTaxAmount() const { return m_totalTaxAmount; }

    // Gestión de items de impuesto
    void addTaxItem(const VerifactuTaxItem &item) { m_taxItems.append(item); }
    const QList<VerifactuTaxItem> &getTaxItems() const { return m_taxItems; }
    void clearTaxItems() { m_taxItems.clear(); }

    // Conversión a JSON para API REST
    QJsonObject toJson() const;

    // Validación
    bool isValid() const;
    QString getValidationError() const;

    // Calcula totales automáticamente
    void calculateTotals();

private:
    // Datos básicos
    QString m_invoiceNumber;
    QDate m_invoiceDate;
    InvoiceType m_invoiceType;
    QString m_description;

    // Emisor
    QString m_sellerNIF;
    QString m_sellerName;

    // Comprador
    QString m_buyerNIF;
    QString m_buyerName;

    // Items de impuesto
    QList<VerifactuTaxItem> m_taxItems;

    // Totales
    double m_totalAmount;
    double m_totalTaxAmount;

    mutable QString m_validationError;

    QString invoiceTypeToString(InvoiceType type) const;
    QString operationTypeToString(VerifactuTaxItem::OperationType type) const;
    QString taxTypeToString(VerifactuTaxItem::TaxType type) const;
};

#endif // VERIFACTUINVOICE_H
