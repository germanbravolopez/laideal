#ifndef VERIFACTUINTEGRATION_H
#define VERIFACTUINTEGRATION_H

#include <QString>
#include <QDate>
#include "verifactumanager.h"

/**
 * @brief Clase de ejemplo que muestra cómo integrar Verifactu con el módulo de facturas
 *
 * Esta clase actúa como puente entre tu módulo de facturas existente y la API de Verifactu,
 * proporcionando métodos de alto nivel para:
 * - Crear y enviar facturas Verifactu
 * - Generar códigos QR
 * - Validar facturas
 *
 * Incluye en tu mainwindow.h o facturas.h:
 * @code
 * #include "verifactuintegration.h"
 *
 * // En la clase principal:
 * private:
 *     VerifactuIntegration *m_verifactuIntegration;
 * @endcode
 *
 * En tu constructor:
 * @code
 * m_verifactuIntegration = new VerifactuIntegration(this);
 * m_verifactuIntegration->initialize();
 * @endcode
 *
 * Para enviar una factura desde tu módulo de facturas:
 * @code
 * // Desde tu código de facturas
 * VerifactuResult result = m_verifactuIntegration->createAndSubmitInvoice(
 *     "F001",                          // número de factura
 *     QDate::currentDate(),            // fecha
 *     "B12345678",                     // NIF comprador
 *     "Cliente S.A.",                  // Nombre comprador
 *     100.0,                           // base imponible
 *     21.0                             // tipo IVA
 * );
 *
 * if (result.isSuccess()) {
 *     qDebug() << "Factura enviada con CSV:" << result.csv;
 *     // Guardar CSV en base de datos
 * } else {
 *     qDebug() << "Error:" << result.errorDescription;
 * }
 * @endcode
 */
class VerifactuIntegration : public QObject
{
    Q_OBJECT

public:
    explicit VerifactuIntegration(QObject *parent = nullptr);
    ~VerifactuIntegration();

    /**
     * @brief Inicializa la integración de Verifactu
     * @return true si se inicializa correctamente
     */
    bool initialize();

    /**
     * @brief Obtiene el gestor de Verifactu
     * @return Puntero al VerifactuManager
     */
    VerifactuManager *getManager() const { return m_manager; }

    /**
     * @brief Método de alto nivel para crear y enviar una factura
     *
     * @param invoiceNumber Número de factura (ej: "F001")
     * @param invoiceDate Fecha de la factura
     * @param buyerNIF NIF del comprador
     * @param buyerName Nombre del comprador
     * @param taxBase Base imponible
     * @param taxRate Tipo de IVA (21, 10, 4, etc.)
     * @param description Descripción de la operación
     * @return Resultado del envío
     */
    VerifactuResult createAndSubmitInvoice(
        const QString &invoiceNumber,
        const QDate &invoiceDate,
        const QString &buyerNIF,
        const QString &buyerName,
        double taxBase,
        double taxRate,
        const QString &description = QString()
    );

    /**
     * @brief Anula una factura previamente enviada
     *
     * @param invoiceNumber Número de factura a anular
     * @param invoiceDate Fecha de la factura
     * @return Resultado de la anulación
     */
    VerifactuResult cancelInvoice(
        const QString &invoiceNumber,
        const QDate &invoiceDate
    );

    /**
     * @brief Genera un código QR para una factura
     *
     * @param invoiceNumber Número de factura
     * @param invoiceDate Fecha de la factura
     * @param totalAmount Importe total
     * @return Resultado con el QR y URL de validación
     */
    VerifactuResult generateQR(
        const QString &invoiceNumber,
        const QDate &invoiceDate,
        double totalAmount
    );

    /**
     * @brief Obtiene la información de configuración actual
     * @return String con información de configuración
     */
    QString getConfigInfo() const;

    /**
     * @brief Verifica si Verifactu está correctamente configurado
     * @return true si está configurado
     */
    bool isConfigured() const;

    /**
     * @brief Obtiene el último error
     * @return Descripción del error
     */
    QString getLastError() const { return m_lastError; }

private:
    VerifactuManager *m_manager;
    QString m_lastError;

    /**
     * @brief Valida que los datos del emisor estén configurados
     * @return true si están configurados
     */
    bool validateEmitterConfiguration();

    /**
     * @brief Carga la configuración del emisor desde la base de datos o configuración
     * @return true si se carga correctamente
     */
    bool loadEmitterConfiguration();
};

#endif // VERIFACTUINTEGRATION_H
