#ifndef VERIFACTUCONFIG_H
#define VERIFACTUCONFIG_H

#include <QString>
#include <QSettings>

/**
 * @brief Clase para gestionar la configuración de Verifactu
 *
 * Esta clase maneja la configuración necesaria para comunicarse con la API REST
 * de Verifactu, incluyendo:
 * - URL del servicio (producción/pruebas)
 * - Clave de servicio
 * - Datos del sistema informático
 * - Información del emisor (NIF, nombre, etc.)
 */
class VerifactuConfig
{
public:
    enum Environment {
        TESTING,    // Entorno de pruebas
        PRODUCTION  // Entorno de producción
    };

    /**
     * @brief Constructor
     * @param configPath Ruta al archivo de configuración (por defecto: en carpeta de aplicación)
     */
    explicit VerifactuConfig(const QString &configPath = QString());

    /**
     * @brief Establece el entorno de ejecución
     * @param env Entorno (TESTING o PRODUCTION)
     */
    void setEnvironment(Environment env);

    /**
     * @brief Obtiene el entorno actual
     */
    Environment getEnvironment() const;

    /**
     * @brief Establece la clave de servicio API REST
     * @param key Clave de servicio obtenida en https://facturae.irenesolutions.com/verifactu/go
     */
    void setServiceKey(const QString &key);

    /**
     * @brief Obtiene la clave de servicio
     */
    QString getServiceKey() const;

    /**
     * @brief Establece los datos del emisor (empresario)
     * @param nif NIF del emisor
     * @param name Nombre del emisor
     * @param companyName Nombre comercial/razón social
     */
    void setEmitterData(const QString &nif, const QString &name, const QString &companyName = QString());

    /**
     * @brief Obtiene el NIF del emisor
     */
    QString getEmitterNIF() const;

    /**
     * @brief Obtiene el nombre del emisor
     */
    QString getEmitterName() const;

    /**
     * @brief Obtiene el nombre comercial del emisor
     */
    QString getEmitterCompanyName() const;

    /**
     * @brief Establece los datos del sistema informático
     * @param name Nombre del sistema
     * @param version Versión
     * @param developer NIF/identificador del desarrollador
     */
    void setSystemData(const QString &name, const QString &version, const QString &developer);

    /**
     * @brief Obtiene el nombre del sistema
     */
    QString getSystemName() const;

    /**
     * @brief Obtiene la versión del sistema
     */
    QString getSystemVersion() const;

    /**
     * @brief Obtiene el identificador del desarrollador
     */
    QString getSystemDeveloper() const;

    /**
     * @brief Obtiene la URL del endpoint de la API
     */
    QString getEndpointUrl() const;

    /**
     * @brief Obtiene la URL para validación de QR
     */
    QString getValidationUrl() const;

    /**
     * @brief Obtiene la URL para obtener QR
     */
    QString getQrUrl() const;

    /**
     * @brief Guarda la configuración en archivo
     */
    void save();

    /**
     * @brief Carga la configuración desde archivo
     */
    void load();

    /**
     * @brief Valida que la configuración sea correcta
     * @return true si la configuración es válida
     */
    bool isValid() const;

    /**
     * @brief Obtiene el mensaje de error si la configuración no es válida
     */
    QString getValidationError() const;

private:
    QSettings *m_settings;
    Environment m_environment;
    QString m_serviceKey;
    QString m_emitterNIF;
    QString m_emitterName;
    QString m_emitterCompanyName;
    QString m_systemName;
    QString m_systemVersion;
    QString m_systemDeveloper;
    mutable QString m_validationError;

    // URLs de los endpoints
    const QString PROD_ENDPOINT = "https://facturae.irenesolutions.com:8050/Kivu/Taxes/Verifactu/Invoices";
    const QString TEST_ENDPOINT = "https://facturae.irenesolutions.com:8050/Kivu/Taxes/Verifactu/Invoices";
    const QString AEAT_VALIDATION_PROD = "https://www2.aeat.es/wlpl/TIKE-CONT/ValidarQR";
    const QString AEAT_VALIDATION_TEST = "https://prewww2.aeat.es/wlpl/TIKE-CONT/ValidarQR";
};

#endif // VERIFACTUCONFIG_H
