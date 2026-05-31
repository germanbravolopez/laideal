#include "verifactutaxitem.h"

#include <QDebug>
#include <cmath>

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
    // Negative base is allowed: a rectificativa por diferencias representing a
    // downward correction (e.g. -20.00 EUR delta) is a legitimate use of this
    // type. AEAT validates the sign in context.

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
