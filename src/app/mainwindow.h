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

class QCheckBox;
class QComboBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QTableWidget;
namespace UiKit { class ResultPanel; }

// Rows the ticket table starts with; "Añadir fila" adds more for a long ticket.
constexpr int kInitialTicketRows = 9;

// The ticket-entry window: client, ticket, garments, Guardar ticket, plus the menus
// that open every other window. Built in code with the shared UiKit style; messages
// go to its result panel, AEAT replies and backups to the status bar.

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
    // Re-prices one row from its garment, service, quantity and size.
    void updateRowPrice(int row);

    bool validateTicket();
    QString removeSpecialChar(QString str);
    void checkClientData();
    // Returns the in-flight reqId, or empty if Verifactu is not configured /
    // submit was rejected. seq selects the submission event: 0 = save-time /
    // full-ticket (InvoiceID = bare n_recibo), >0 = a partial-pay event being
    // recovered (InvoiceID "<ticketNum>-<seq>", that seq's amount).
    QString verifactuSubmitInvoice(const QString &ticketNum, const QDate &invoiceDate,
                                   double totalAmount, int seq = 0);
    // Inserts the ticket's garments; storedTotal = the sum of the stored amounts.
    // False (stops at the first failed insert) when the ticket was not stored whole.
    bool saveTicket(double &storedTotal);
    // Each returns whether both copies reached the printer.
    bool printRecibo();
    bool printFra(const QPixmap &qrCode = QPixmap());
    void onVerifactuRequestFinished(const QString &requestId, const VerifactuResult &result);

    // Widgets
    void on_pb_payment_toggled(bool checked);
    void on_pb_save_clicked();
    void on_pb_reset_clicked();
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
    // Direct AEAT connection (research): the self-test window from Configuración, and
    // the chain hand-over after the gateway when the connection starts.
    void runAeatSelfTest(QWidget *parent, const QString &nif, const QString &name, const QString &thumbprint,
                         const QString &certificateFile, const QString &certificatePassword);
    void continueDirectChain();

    // The window's widgets, named as in the former .ui form (the on_<name>_<signal>
    // slots connect by name and the e2e bench finds them by it).
    struct Widgets {
        QLabel *lbl_title = nullptr;
        QComboBox *cb_client = nullptr;
        QLineEdit *le_phone = nullptr, *le_mobile = nullptr, *le_addr = nullptr;
        QLineEdit *le_nr_ticket = nullptr, *le_cost_total = nullptr;
        QDateEdit *de_date_recep = nullptr;
        QCheckBox *pb_payment = nullptr;          // paid at drop-off
        QLabel *lbl_payment_badge = nullptr;
        QTableWidget *table_ticket = nullptr;
        QPushButton *pb_add_row = nullptr, *pb_save = nullptr, *pb_reset = nullptr;
        QMenu *menuArchivo = nullptr;
        QAction *actionMostrar_log = nullptr;
    };
    void buildUi();
    int rowOfCellWidget(QObject *widget, int column) const;

    Widgets *ui = nullptr;
    UiKit::ResultPanel *m_result = nullptr;
    QString m_clientNote;   // set by checkClientData, shown with the save result
    VerifactuIntegration *m_verifactuIntegration;
    // Async submit tracking: reqId -> (ticket number, verifactu_invoice_seq), so
    // the requestFinished handler can patch exactly the rows of that submission
    // event (a save-time submit and a recovered partial-pay event of the same
    // ticket differ only by seq).
    // printedWithoutQr: the bounded wait expired and a QR-less recibo was already
    // handed to the customer, so a late success has to say the factura is now
    // printable rather than just reporting the CSV.
    struct PendingSubmit { QString ticketNum; int seq = 0; bool printedWithoutQr = false; };
    QHash<QString, PendingSubmit> m_pendingSubmits;
    Updater *m_updater;
    BackupManager *m_backupManager;
};

#endif // MAINWINDOW_H
