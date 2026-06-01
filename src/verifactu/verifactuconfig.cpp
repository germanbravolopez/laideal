#include "verifactuconfig.h"

#include <QDebug>
#include <QFile>
#include <QStandardPaths>

VerifactuConfig::VerifactuConfig()
    : m_environment(TESTING),
      m_systemName("LAIDEAL"),
      m_systemVersion(QString(PROJECT_VERSION))
{
    // One-time cleanup of the legacy QSettings INI written by pre-8.5 versions
    // of this class. The INI was a stale plaintext copy of credentials already
    // stored in ~/.laideal_settings.json and is no longer consulted. Removing
    // it shrinks the credential footprint on disk.
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

void VerifactuConfig::setSystemData(const QString &name, const QString &version)
{
    m_systemName = name;
    m_systemVersion = version;
}

QString VerifactuConfig::getSystemName() const
{
    return m_systemName;
}

QString VerifactuConfig::getSystemVersion() const
{
    return m_systemVersion;
}

QString VerifactuConfig::getEndpointUrl() const
{
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
    if (m_serviceKey.isEmpty()) {
        m_validationError = "Clave de servicio no configurada. Obténla en https://facturae.irenesolutions.com/verifactu/go";
        return false;
    }

    if (m_emitterNIF.isEmpty()) {
        m_validationError = "NIF del emisor no configurado";
        return false;
    }

    if (m_emitterName.isEmpty()) {
        m_validationError = "Nombre del emisor no configurado";
        return false;
    }

    return true;
}

QString VerifactuConfig::getValidationError() const
{
    return m_validationError;
}
