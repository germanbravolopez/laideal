#include "verifactuinvoice.h"
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <cmath>

// ============================================================================
// VerifactuTaxItem Implementation
// ============================================================================

VerifactuTaxItem::VerifactuTaxItem()
    : m_taxType(VAT), m_taxRate(0.0), m_taxBase(0.0), m_taxAmount(0.0),
      m_operationType(S1)
{
}

QJsonObject VerifactuTaxItem::toJson() const
{
    QJsonObject json;
    json["TaxRate"] = m_taxRate;
    json["TaxBase"] = m_taxBase;
    json["TaxAmount"] = m_taxAmount;
    json["TaxType"] = VerifactuTaxItem::operationTypeToString(m_operationType);

    if (!m_description.isEmpty()) {
        json["Description"] = m_description;
    }

    return json;
}

bool VerifactuTaxItem::isValid() const
{
    if (m_taxBase < 0) {
        m_validationError = "La base imponible no puede ser negativa";
        return false;
    }

    if (m_operationType != EXEMPT && m_taxRate < 0) {
        m_validationError = "El tipo de impuesto no puede ser negativo";
        return false;
    }

    if (m_operationType != EXEMPT && std::abs((m_taxBase * m_taxRate / 100.0) - m_taxAmount) > 0.01)
        qWarning() << "Tax amount does not match base * rate";

    return true;
}

QString VerifactuTaxItem::getValidationError() const
{
    return m_validationError;
}

QString VerifactuTaxItem::operationTypeToString(OperationType type)
{
    switch (type) {
        case S1: return "S1";  // subject to tax (standard)
        case S2: return "S2";  // reverse charge
        case N1: return "N1";  // not subject (art. 7, 14)
        case N2: return "N2";  // not subject (location rules)
        case EXEMPT: return "E1";  // exempt
        default: return "S1";
    }
}

// ============================================================================
// VerifactuInvoice Implementation
// ============================================================================

VerifactuInvoice::VerifactuInvoice()
    : m_invoiceType(NORMAL), m_totalAmount(0.0), m_totalTaxAmount(0.0)
{
}

VerifactuInvoice::~VerifactuInvoice()
{
}

void VerifactuInvoice::calculateTotals()
{
    m_totalTaxAmount = 0.0;
    m_totalAmount = 0.0;

    for (const auto &item : m_taxItems) {
        m_totalTaxAmount += item.getTaxAmount();
        m_totalAmount += item.getTaxBase();
    }

    m_totalAmount += m_totalTaxAmount;
}

QJsonObject VerifactuInvoice::toJson() const
{
    QJsonObject json;

    json["InvoiceID"] = m_invoiceNumber;
    json["InvoiceDate"] = m_invoiceDate.toString(Qt::ISODate);
    json["InvoiceType"] = invoiceTypeToString(m_invoiceType);
    json["SellerID"] = m_sellerNIF;
    json["CompanyName"] = m_sellerName; // SellerName is not used

    if (!m_buyerNIF.isEmpty()) {
        json["BuyerID"] = m_buyerNIF;
        json["BuyerName"] = m_buyerName;
    }

    if (!m_description.isEmpty())
        json["Text"] = m_description;

    QJsonArray taxItems;
    for (const auto &item : m_taxItems)
        taxItems.append(item.toJson());
    json["TaxItems"] = taxItems;

    json["TotalAmount"] = m_totalAmount;
    json["TotalTaxAmount"] = m_totalTaxAmount;

    return json;
}

bool VerifactuInvoice::isValid() const
{
    if (m_invoiceNumber.isEmpty()) {
        m_validationError = "Número de factura no configurado";
        return false;
    }

    if (m_invoiceDate.isNull()) {
        m_validationError = "Fecha de factura no configurada";
        return false;
    }

    if (m_sellerNIF.isEmpty()) {
        m_validationError = "NIF del vendedor no configurado";
        return false;
    }

    if (m_sellerName.isEmpty()) {
        m_validationError = "Nombre del vendedor no configurado";
        return false;
    }

    // buyer required except for simplified invoices (F2)
    if (m_invoiceType != SIMPLIFIED) {
        if (m_buyerNIF.isEmpty()) {
            m_validationError = "NIF del comprador no configurado";
            return false;
        }
        if (m_buyerName.isEmpty()) {
            m_validationError = "Nombre del comprador no configurado";
            return false;
        }
    }

    if (m_taxItems.isEmpty()) {
        m_validationError = "La factura debe tener al menos un item de impuesto";
        return false;
    }

    for (const auto &item : m_taxItems) {
        if (!item.isValid()) {
            m_validationError = "Item de impuesto inválido: " + item.getValidationError();
            return false;
        }
    }

    return true;
}

QString VerifactuInvoice::getValidationError() const
{
    return m_validationError;
}

QString VerifactuInvoice::invoiceTypeToString(InvoiceType type) const
{
    switch (type) {
        case NORMAL:        return "F1";  // standard invoice
        case SIMPLIFIED:    return "F2";  // simplified invoice
        case SUBSTITUTE:    return "F3";  // substitute for simplified
        case RECTIFICATION: return "R1";  // corrective invoice
        default:            return "F1";
    }
}

QString VerifactuInvoice::operationTypeToString(VerifactuTaxItem::OperationType type) const
{
    return VerifactuTaxItem::operationTypeToString(type);
}

QString VerifactuInvoice::taxTypeToString(VerifactuTaxItem::TaxType type) const
{
    switch (type) {
        case VerifactuTaxItem::VAT: return "VAT";
        case VerifactuTaxItem::IGIC: return "IGIC";
        case VerifactuTaxItem::OTHER: return "OTHER";
        default: return "VAT";
    }
}
