#include "verifactuinvoice.h"

#include <QJsonArray>
#include <QJsonObject>

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

    // Rectificativa metadata (R1-R5). RectificationType is mandatory when the
    // invoice is corrective; RectificationTaxBase/Amount only matter for the
    // substitution variant (S) - they carry the values being replaced.
    if (isRectificationInvoiceType(m_invoiceType)) {
        json["RectificationType"] = rectificationTypeToString(
            m_hasRectification ? m_rectificationType : BY_DIFFERENCES);
        if (m_hasRectification && m_rectificationType == BY_SUBSTITUTION) {
            json["RectificationTaxBase"]   = m_rectificationTaxBase;
            json["RectificationTaxAmount"] = m_rectificationTaxAmount;
        }
    }

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

    // Buyer required except for F2 (simplified) and rectificativas of simplified
    // invoices (R5 by definition, plus R1-R4 when the original was simplified -
    // La Ideal only issues F2, so all our rectificativas inherit no-buyer).
    if (m_invoiceType != SIMPLIFIED && !isRectificationInvoiceType(m_invoiceType)) {
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
        case NORMAL:           return "F1";  // standard invoice
        case SIMPLIFIED:       return "F2";  // simplified invoice
        case SUBSTITUTE:       return "F3";  // substitute for simplified
        case RECTIFICATION_R1: return "R1";  // corrective - legal error
        case RECTIFICATION_R2: return "R2";  // corrective - buyer insolvency
        case RECTIFICATION_R3: return "R3";  // corrective - bad debts
        case RECTIFICATION_R4: return "R4";  // corrective - other
        case RECTIFICATION_R5: return "R5";  // corrective on simplified invoices
        default:               return "F1";
    }
}

QString VerifactuInvoice::rectificationTypeToString(RectificationType type)
{
    switch (type) {
        case BY_SUBSTITUTION: return "S";
        case BY_DIFFERENCES:  return "I";
        default:              return "I";
    }
}

bool VerifactuInvoice::isRectificationInvoiceType(InvoiceType type)
{
    switch (type) {
        case RECTIFICATION_R1:
        case RECTIFICATION_R2:
        case RECTIFICATION_R3:
        case RECTIFICATION_R4:
        case RECTIFICATION_R5:
            return true;
        default:
            return false;
    }
}


