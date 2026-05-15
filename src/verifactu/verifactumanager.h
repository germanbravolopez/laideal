#ifndef VERIFACTUMANAGER_H
#define VERIFACTUMANAGER_H

#include <QString>
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPixmap>
#include "verifactuconfig.h"
#include "verifactuinvoice.h"

/**
 * @brief Resultado de una operación Verifactu
 */
struct VerifactuResult
{
    enum Status {
        SUCCESS,        // Operación exitosa
        PENDING,        // Operación pendiente
        ERROR,          // Error en la operación
        NETWORK_ERROR,  // Error de conectividad
        INVALID_CONFIG  // Configuración inválida
    };

    Status status;
    QString csv;                    // Código de Seguridad de la Versión (si exitosa)
    QString errorCode;              // Código de error (si falla)
    QString errorDescription;       // Descripción del error
    QString validationUrl;          // URL para validar en la AEAT
    QPixmap qrCode;                 // Código QR
    QString rawResponse;            // Respuesta completa del servidor

    VerifactuResult() : status(INVALID_CONFIG) {}

    bool isSuccess() const { return status == SUCCESS; }
    bool isError() const { return status == ERROR || status == NETWORK_ERROR || status == INVALID_CONFIG; }
};

/**
 * @brief Gestor principal para la integración con Verifactu
 *
 * Esta clase proporciona métodos para:
 * - Validar y enviar facturas a la AEAT mediante la API REST de Verifactu
 * - Generar códigos QR para validación
 * - Obtener información sobre el estado del envío
 *
 * Uso:
 * @code
 * VerifactuManager manager("path/to/config.ini");
 * manager.getConfig()->setServiceKey("mi_clave_servicio");
 * manager.getConfig()->setEmitterData("B12345678", "Mi Empresa");
 *
 * VerifactuInvoice invoice;
 * invoice.setInvoiceNumber("F001");
 * invoice.setInvoiceDate(QDate::currentDate());
 * // ... configurar más datos ...
 *
 * VerifactuResult result = manager.submitInvoice(invoice);
 * if (result.isSuccess()) {
 *     qDebug() << "Factura enviada con CSV:" << result.csv;
 * }
 * @endcode
 */
class VerifactuManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Constructor
     * @param configPath Ruta al archivo de configuración
     * @param parent Objeto padre de Qt
     */
    explicit VerifactuManager(const QString &configPath = QString(), QObject *parent = nullptr);
    ~VerifactuManager();

    /**
     * @brief Obtiene la configuración de Verifactu
     * @return Puntero a la configuración
     */
    VerifactuConfig *getConfig() const { return m_config; }

    /**
     * @brief Envía una factura a la AEAT mediante Verifactu
     * @param invoice Factura a enviar
     * @return Resultado del envío
     */
    VerifactuResult submitInvoice(const VerifactuInvoice &invoice);

    /**
     * @brief Envía múltiples facturas a la AEAT
     * @param invoices Lista de facturas
     * @return Resultado del envío del lote
     */
    VerifactuResult submitInvoices(const QList<VerifactuInvoice> &invoices);

    /**
     * @brief Anula una factura previamente enviada
     * @param invoiceNumber Número de factura
     * @param invoiceDate Fecha de factura
     * @param sellerNIF NIF del emisor
     * @return Resultado de la anulación
     */
    VerifactuResult cancelInvoice(const QString &invoiceNumber, const QDate &invoiceDate, const QString &sellerNIF);

    /**
     * @brief Genera un código QR para validación de una factura
     * @param invoice Factura para la que generar el QR
     * @return Resultado con el QR y la URL de validación
     */
    VerifactuResult generateQRCode(const VerifactuInvoice &invoice);

    /**
     * @brief Obtiene la URL de validación de una factura en la AEAT
     * @param invoice Factura
     * @return URL de validación
     */
    QString getValidationUrl(const VerifactuInvoice &invoice) const;

    /**
     * @brief Obtiene el último error de conectividad o procesamiento
     * @return Descripción del error
     */
    QString getLastError() const { return m_lastError; }

    /**
     * @brief Realiza una prueba de conectividad con la API de Verifactu
     * @return true si la conexión es exitosa
     */
    bool testConnection();

    /**
     * @brief Obtiene información sobre la configuración actual
     * @return String con información de configuración
     */
    QString getConfigurationInfo() const;

private slots:
    void onNetworkReply(QNetworkReply *reply);

private:
    VerifactuConfig *m_config;
    QNetworkAccessManager *m_networkManager;
    QString m_lastError;
    VerifactuResult m_lastResult;

    /**
     * @brief Crea las cabeceras HTTP necesarias para la API REST
     * @return Headers HTTP
     */
    QNetworkRequest createNetworkRequest(const QString &endpoint) const;

    /**
     * @brief Procesa la respuesta JSON del servidor
     * @param response Respuesta JSON como QByteArray
     * @param isQrRequest true si es una solicitud de QR
     * @return VerifactuResult con los datos procesados
     */
    VerifactuResult processResponse(const QByteArray &response, bool isQrRequest = false);

    /**
     * @brief Valida la configuración antes de enviar
     * @return true si la configuración es válida
     */
    bool validateConfiguration();

    /**
     * @brief Codifica una imagen como base64 para transmisión
     * @param pixmap Imagen a codificar
     * @return Datos base64
     */
    QString encodeImageToBase64(const QPixmap &pixmap) const;

    /**
     * @brief Decodifica una imagen desde base64
     * @param data Datos base64
     * @return QPixmap con la imagen decodificada
     */
    QPixmap decodeImageFromBase64(const QString &data) const;
};

#endif // VERIFACTUMANAGER_H
