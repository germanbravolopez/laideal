#include "verifactuconfig.h"
#include <QStandardPaths>
#include <QDir>
#include <QDebug>

VerifactuConfig::VerifactuConfig(const QString &configPath)
    : m_environment(TESTING), m_systemName("LAIDEAL"), m_systemVersion(QString(PROJECT_VERSION)), m_systemDeveloper("LAIDEAL")
{
    QString path = configPath.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/verifactu_config.ini"
        : configPath;

    QDir().mkpath(QFileInfo(path).dir().absolutePath());
    m_settings = new QSettings(path, QSettings::IniFormat);

    // Cargar configuración guardada
    load();
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

void VerifactuConfig::setEmitterData(const QString &nif, const QString &name, const QString &companyName)
{
    m_emitterNIF = nif;
    m_emitterName = name;
    m_emitterCompanyName = companyName.isEmpty() ? name : companyName;
}

QString VerifactuConfig::getEmitterNIF() const
{
    return m_emitterNIF;
}

QString VerifactuConfig::getEmitterName() const
{
    return m_emitterName;
}

QString VerifactuConfig::getEmitterCompanyName() const
{
    return m_emitterCompanyName;
}

void VerifactuConfig::setSystemData(const QString &name, const QString &version, const QString &developer)
{
    m_systemName = name;
    m_systemVersion = version;
    m_systemDeveloper = developer;
}

QString VerifactuConfig::getSystemName() const
{
    return m_systemName;
}

QString VerifactuConfig::getSystemVersion() const
{
    return m_systemVersion;
}

QString VerifactuConfig::getSystemDeveloper() const
{
    return m_systemDeveloper;
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

void VerifactuConfig::save()
{
    m_settings->beginGroup("Verifactu");
    m_settings->setValue("Environment", static_cast<int>(m_environment));
    m_settings->setValue("ServiceKey", m_serviceKey);

    m_settings->beginGroup("Emitter");
    m_settings->setValue("NIF", m_emitterNIF);
    m_settings->setValue("Name", m_emitterName);
    m_settings->setValue("CompanyName", m_emitterCompanyName);
    m_settings->endGroup();

    m_settings->beginGroup("System");
    m_settings->setValue("Name", m_systemName);
    m_settings->setValue("Version", m_systemVersion);
    m_settings->setValue("Developer", m_systemDeveloper);
    m_settings->endGroup();

    m_settings->endGroup();
    m_settings->sync();

    qDebug() << "Configuración de Verifactu guardada correctamente";
}

void VerifactuConfig::load()
{
    m_settings->beginGroup("Verifactu");

    m_environment = static_cast<Environment>(m_settings->value("Environment", TESTING).toInt());
    m_serviceKey = m_settings->value("ServiceKey", "").toString();

    m_settings->beginGroup("Emitter");
    m_emitterNIF = m_settings->value("NIF", "").toString();
    m_emitterName = m_settings->value("Name", "").toString();
    m_emitterCompanyName = m_settings->value("CompanyName", "").toString();
    m_settings->endGroup();

    m_settings->beginGroup("System");
    m_systemName = m_settings->value("Name", "LAIDEAL").toString();
    m_systemVersion = m_settings->value("Version", PROJECT_VERSION).toString();
    m_systemDeveloper = m_settings->value("Developer", "LAIDEAL").toString();
    m_settings->endGroup();

    m_settings->endGroup();
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
