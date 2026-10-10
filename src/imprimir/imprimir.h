#ifndef IMPRIMIR_H
#define IMPRIMIR_H

#include <QDialog>
#include <QSqlDatabase>
#include <QSqlQueryModel>
#include <QDateTime>
#include <QFile>
#include <QApplication>
#include <QPixmap>
#include <QByteArray>
#include <QStringList>
#include <QLineEdit>   // le_n_ticket is public: callers set the receipt number

#include "sql_lite.h"
#include "verifactuintegration.h"

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QPushButton;
namespace UiKit { class ResultPanel; }

// `ingresos` column indices come from sql_lite.h (INGRESOS_COL_*).

// Two roles. As a dialog (Imprimir -> Recibo / Factura / Factura completa) it is
// built in code with the shared UiKit style: the receipt number, the full-invoice
// details (address, DNI), the choice among several partial-payment invoices and
// the shop copy are fields of the window, and every outcome is shown in its
// result panel. As a print engine (MainWindow save, Cobrar, Recogida) callers set
// the public fields and call getTicketInfo / buildTicket / printTicket without
// showing it; printer problems then still pop up, since there is no panel.
class Imprimir : public QDialog
{
    Q_OBJECT

public:
    QSqlQueryModel *sqlQueryModel = nullptr;
    bool isRecibo = true, isCompleteInvoice = false;
    VerifactuIntegration *verifactuIntegration = nullptr;
    QPixmap qrCode;
    // -1 (default): print every row of n_recibo (legacy / full-ticket flow).
    // >=0: scope getTicketInfo to rows with verifactu_invoice_seq = invoiceSeq,
    // so partial-payment events (8.5+) print only the garments charged that time.
    int invoiceSeq = -1;
    Imprimir(const QSqlDatabase &database, QWidget *parent = nullptr);

    // Public functions
    void getTicketInfo();
    // Build the ESC/POS byte stream for one ticket copy into m_ticketBytes
    // (replaces the old Excel generation). printTicket() then sends it RAW.
    void buildTicket(bool copyForClient, bool addPayedInfo);
    // Sends the built ticket; false when it could not be printed.
    bool printTicket();
    // The ESC/POS bytes of the last buildTicket() (tests read the rendered text).
    QByteArray ticketBytes() const { return m_ticketBytes; }
    QPixmap resolveQrCode();
    // Literal AEAT InvoiceID for the loaded rows: first non-empty
    // verifactu_invoice_id, else bare n_recibo (legacy / never submitted).
    QString displayInvoiceId() const;

    // The receipt number, set by the engine callers and typed in the dialog.
    QLineEdit *le_n_ticket = nullptr;

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void onPrintClicked();

private:
    void buildUi();
    bool checkTicketPaid(int row);
    bool checkAnyItemPaid();
    // A printer problem: into the result panel while the dialog is shown, else a message box.
    void reportPrinterProblem(const QString &text, bool critical);
    // Prints the built ticket if printing is enabled; returns whether it reached the printer.
    bool sendIfEnabled();

    // Printable width in dots derived from AppSettings::paperWidthMm()
    // (576 @80mm, 420 @58mm). Drives the renderer's column layout.
    int paperDots() const;

    QSqlDatabase db;
    // ESC/POS bytes for the most recently built copy; printTicket() sends these.
    QByteArray m_ticketBytes;

    QLabel      *m_lblIntro = nullptr;
    QGroupBox   *m_grpClient = nullptr;    // full invoice only
    QLineEdit   *m_leAddress = nullptr;
    QLineEdit   *m_leDni = nullptr;
    QLabel      *m_lblEvent = nullptr;     // several partial-payment invoices
    QComboBox   *m_cbEvent = nullptr;
    QCheckBox   *m_chkShopCopy = nullptr;  // recibo only
    QPushButton *m_btnPrint = nullptr;
    UiKit::ResultPanel *m_lblResult = nullptr;
    QString      m_eventsTicket;           // the ticket m_cbEvent was filled for
    QStringList  m_printProblems;          // collected while the dialog is shown
};

#endif // IMPRIMIR_H
