#include "verifactuconfig.h"

#include <QDebug>
#include <QFile>
#include <QStandardPaths>

VerifactuConfig::VerifactuConfig()
    : m_environment(TESTING),
      m_systemName("LAIDEAL"),
      m_systemVersion(QString(PROJECT_VERSION))
{
}

void VerifactuConfig::removeLegacyIni()
{
    // The QSettings INI of pre-8.5 versions was a stale plaintext copy of credentials
    // already in ~/.laideal_settings.json and is no longer read.
    const QString legacyIni = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                              + "/verifactu_config.ini";
    if (QFile::exists(legacyIni)) {
        if (QFile::remove(legacyIni))
            qDebug() << "VerifactuConfig: removed legacy INI" << legacyIni;
        else
            qWarning() << "VerifactuConfig: failed to remove legacy INI" << legacyIni;
    }
}

void VerifactuConfig::setEnvironment(Environment env)
{
    m_environment = env;
}

VerifactuConfig::Environment VerifactuConfig::getEnvironment() const
{
    return m_environment;
}

void VerifactuConfig::setServiceKey(const QString &key)
{
    m_serviceKey = key;
}

QString VerifactuConfig::getServiceKey() const
{
    return m_serviceKey;
}

void VerifactuConfig::setEmitterData(const QString &nif, const QString &name)
{
    m_emitterNIF = nif;
    m_emitterName = name;
}

QString VerifactuConfig::getEmitterNIF() const
{
    return m_emitterNIF;
}

QString VerifactuConfig::getEmitterName() const
{
    return m_emitterName;
}

QString VerifactuConfig::getSystemName() const
{
    return m_systemName;
}

QString VerifactuConfig::getSystemVersion() const
{
    return m_systemVersion;
}

static QString s_endpointOverride;

void VerifactuConfig::setEndpointOverride(const QString &baseUrl)
{
    s_endpointOverride = baseUrl;
}

QString VerifactuConfig::getEndpointUrl() const
{
    if (!s_endpointOverride.isEmpty())
        return s_endpointOverride;
    return (m_environment == TESTING ? TEST_ENDPOINT : PROD_ENDPOINT);
}

QString VerifactuConfig::getValidationUrl() const
{
    return (m_environment == TESTING ? AEAT_VALIDATION_TEST : AEAT_VALIDATION_PROD);
}

QString VerifactuConfig::getQrUrl() const
{
    return getEndpointUrl() + "/GetQrCode";
}

bool VerifactuConfig::isValid() const
{
    return getValidationError().isEmpty();
}

QString VerifactuConfig::getValidationError() const
{
    if (m_serviceKey.isEmpty())
        return "Clave de servicio no configurada. Obténla en https://facturae.irenesolutions.com/verifactu/go";
    if (m_emitterNIF.isEmpty())
        return "NIF del emisor no configurado";
    if (m_emitterName.isEmpty())
        return "Nombre del emisor no configurado";
    return QString();
}
