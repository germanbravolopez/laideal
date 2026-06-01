#ifndef VERIFACTUCONFIG_H
#define VERIFACTUCONFIG_H

#include <QString>

// In-memory Verifactu API configuration: credentials, emitter data, system info,
// and environment. Populated at startup by VerifactuIntegration from AppSettings
// (the JSON at ~/.laideal_settings.json); not persisted by this class itself.
class VerifactuConfig
{
public:
    enum Environment {
        TESTING,
        PRODUCTION
    };

    VerifactuConfig();

    void setEnvironment(Environment env);
    Environment getEnvironment() const;

    void setServiceKey(const QString &key);
    QString getServiceKey() const;

    void setEmitterData(const QString &nif, const QString &name);
    QString getEmitterNIF() const;
    QString getEmitterName() const;

    void setSystemData(const QString &name, const QString &version);
    QString getSystemName() const;
    QString getSystemVersion() const;

    QString getEndpointUrl() const;
    QString getValidationUrl() const;
    QString getQrUrl() const;

    bool isValid() const;
    QString getValidationError() const;

private:
    Environment m_environment;
    QString m_serviceKey;
    QString m_emitterNIF;
    QString m_emitterName;
    QString m_systemName;
    QString m_systemVersion;
    mutable QString m_validationError;

    const QString PROD_ENDPOINT = "https://facturae.irenesolutions.com:8050/Kivu/Taxes/Verifactu/Invoices";
    const QString TEST_ENDPOINT = "https://facturae.irenesolutions.com:8050/Kivu/Taxes/Verifactu/Invoices";
    const QString AEAT_VALIDATION_PROD = "https://www2.aeat.es/wlpl/TIKE-CONT/ValidarQR";
    const QString AEAT_VALIDATION_TEST = "https://prewww2.aeat.es/wlpl/TIKE-CONT/ValidarQR";
};

#endif // VERIFACTUCONFIG_H
