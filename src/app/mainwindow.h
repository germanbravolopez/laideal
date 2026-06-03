#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QAbstractButton>
#include <QSqlDatabase>
#include <QDebug>
#include <QMessageBox>
#include <QStyleFactory>
#include <QHash>
#include "verifactuintegration.h"
#include "cancelinvoicedialog.h"
#include "rectifyinvoicedialog.h"
#include "updater.h"
#include "backup_manager.h"

#define TABLE_TICKET_QNTY   0
#define TABLE_TICKET_GARM   1
#define TABLE_TICKET_SIZE   2
#define TABLE_TICKET_SERV   3
#define TABLE_TICKET_OBSE   4
#define TABLE_TICKET_PRIC   5

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    QSqlDatabase db;
    int pbAddedRows = 0;

private slots:
    void mainwindowInitialSettings();
    void initializeVerifactu();
    void resetAllContents();

    // Custom functions
    void setNextTicketNumber();
    void populateCbClient();
    void resizeTable();
    void setServiceToCb(int initialRow);
    void setGarmentToCbAndPopulate(int initialRow);
    void setGarmentPrice(int garmentRow, QString garmentText, QString serviceText);

    void cbGarmChanged(const QString &text);
    void cbServChanged(const QString &text);

    bool validateTicket();
    QString removeSpecialChar(QString str);
    void checkClientData();
    // Returns the in-flight reqId, or empty if Verifactu is not configured /
    // submit was rejected. seq selects the submission event: 0 = save-time /
    // full-ticket (InvoiceID = bare n_recibo), >0 = a partial-pay event being
    // recovered (InvoiceID "<ticketNum>-<seq>", that seq's amount).
    QString verifactuSubmitInvoice(const QString &ticketNum, const QDate &invoiceDate,
                                   double totalAmount, int seq = 0);
    void saveTicket();
    void printRecibo();
    void printFra(const QPixmap &qrCode = QPixmap());
    void onVerifactuRequestFinished(const QString &requestId, const VerifactuResult &result);

    // Widgets
    void on_pb_payment_toggled(bool checked);
    void on_bb_save_reset_clicked(QAbstractButton *button);
    void on_cb_client_editTextChanged(const QString &arg1);
    void on_table_ticket_cellChanged(int row, int column);
    void on_pb_add_row_clicked();

    // Taskbar
    void on_actionCerrar_triggered();
    void on_actionIngresos_triggered();
    void on_actionGastos_triggered();
    void repopulatePrendas();
    void on_actionListado_de_prendas_triggered();
    void repopulateClientes();
    void on_actionListado_de_clientes_triggered();
    void on_actionListado_de_proveedores_triggered();
    void on_actionListado_de_servicios_triggered();
    void on_actionRecogida_de_prendas_triggered();
    void on_actionRecibo_triggered();
    void on_actionFactura_triggered();
    void on_actionFactura_completa_triggered();
    void on_actionGenerar_contabilidad_triggered();
    void on_actionRevertir_contabilidad_triggered();
    void on_actionFormulario_facturas_triggered();
    void on_actionLimpiar_base_de_datos_triggered();
    void cleanDatabase(bool print);
    void on_actionAnadir_nuevas_prendas_triggered();
    void on_actionCrear_hash_en_ingresos_triggered();
    void on_actionAnular_factura_verifactu_triggered();
    void on_actionRectificar_factura_verifactu_triggered();
    void on_actionExportar_registros_aeat_triggered();
    void on_actionMostrar_log_triggered();
    void on_actionAcerca_de_Verifactu_triggered();
    void on_actionBuscar_actualizaciones_triggered();
    void on_actionNotas_de_la_version_triggered();
    void on_actionHacer_copia_de_seguridad_triggered();

    // Updater signal handlers
    void onUpdateAvailable(const QString &latestVersion,
                           const QString &releaseNotes,
                           const QUrl &installerUrl);
    void onUpdaterNoUpdateAvailable();
    void onUpdaterCheckFailed(const QString &error);

private:
    Ui::MainWindow *ui;
    VerifactuIntegration *m_verifactuIntegration;
    // Async submit tracking: reqId -> (ticket number, verifactu_invoice_seq), so
    // the requestFinished handler can patch exactly the rows of that submission
    // event (a save-time submit and a recovered partial-pay event of the same
    // ticket differ only by seq).
    struct PendingSubmit { QString ticketNum; int seq = 0; };
    QHash<QString, PendingSubmit> m_pendingSubmits;
    Updater *m_updater;
    BackupManager *m_backupManager;
};

#endif // MAINWINDOW_H
