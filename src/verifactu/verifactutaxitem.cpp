#include "verifactutaxitem.h"

#include <QDebug>
#include <cmath>

VerifactuTaxItem::VerifactuTaxItem()
    : m_taxRate(0.0), m_taxBase(0.0), m_taxAmount(0.0)
{
}

QJsonObject VerifactuTaxItem::toJson() const
{
    QJsonObject json;
    json["TaxRate"] = m_taxRate;
    json["TaxBase"] = m_taxBase;
    json["TaxAmount"] = m_taxAmount;
    json["TaxType"] = QStringLiteral("S1");

    return json;
}

bool VerifactuTaxItem::isValid() const
{
    // Negative base is allowed: a rectificativa por diferencias representing a
    // downward correction (e.g. -20.00 EUR delta) is a legitimate use of this
    // type. AEAT validates the sign in context.

    if (m_taxRate < 0) {
        m_validationError = "El tipo de impuesto no puede ser negativo";
        return false;
    }

    if (std::abs((m_taxBase * m_taxRate / 100.0) - m_taxAmount) > 0.01)
        qWarning() << "Tax amount does not match base * rate";

    return true;
}

QString VerifactuTaxItem::getValidationError() const
{
    return m_validationError;
}

