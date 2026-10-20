#ifndef RECTIFYINVOICEDIALOG_H
#define RECTIFYINVOICEDIALOG_H

#include <QDialog>
#include <QSqlDatabase>
#include <QDate>
#include "verifactuintegration.h"
#include "sql_lite.h"

class QLineEdit;
class QLabel;
class QPushButton;
class QComboBox;
class QRadioButton;
class QDateEdit;
class QDoubleSpinBox;
class QGroupBox;

// Factura rectificativa UI (R1-R5). Models CancelInvoiceDialog: search a ticket
// by n_recibo, fill a small form (type, sustitucion/diferencias, corrected amount),
// claim the new number as a PENDIENTE row, submit to AEAT asynchronously, patch the
// row with the reply and (for sustitucion) mark the original RECTIFICADA. Art. 8.2.a
// RD 1007/2023 - corrections must use anulacion or rectificativa.
class RectifyInvoiceDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RectifyInvoiceDialog(const QSqlDatabase &database, QWidget *parent = nullptr);
    // Closed with the gateway's answer still due: the row becomes an error to redo.
    ~RectifyInvoiceDialog() override;

    VerifactuIntegration *m_verifactu = nullptr;

protected:
    // Esc, the window's X and Cerrar all end here: refused while AEAT has not answered,
    // or the outcome of an invoice AEAT may already hold would be lost.
    void done(int result) override;

private slots:
    void onSearchClicked();
    void onRectifyClicked();
    void onRectificationModeChanged();
    void onVerifactuRequestFinished(const QString &requestId, const VerifactuResult &result);

private:
    // The direct client stored the rectificativa and sends it later: no answer yet, not a failure.
    bool queuedWithoutAnswer(const VerifactuResult &result) const;
    void setFormEnabled(bool enabled);
    // Inserts the rectificativa row at submit time with estado=PENDIENTE so the
    // n_recibo is claimed locally before the AEAT round-trip - prevents a regular
    // MainWindow::saveTicket from re-using the same number while we wait.
    RectificativaRow rectificativaRow() const;
    bool insertPlaceholderRow();   // false when the row was not written: nothing is sent
    // The refused rectificativa's own row, updated for its corrected resend.
    bool reusePlaceholderRow();
    // A refused rectificativa redone: its row reused, or resent as filed from a closed
    // quarter. False (reason shown) when it cannot be.
    bool prepareRefusedResend(const QString &refused, double amountWithIva, bool isSubstitution);
    // Patches the row inserted by insertPlaceholderRow with the AEAT reply: csv /
    // xml / hash / url + estado=ENVIADA on success, or estado=ERROR + description
    // on failure. For substitution-mode success it also marks the original rows
    // RECTIFICADA.
    // Returns false when AEAT accepted but the local RECTIFICADA mark failed.
    bool applyRectificationResult(const VerifactuResult &result);

    // Top row - ticket lookup
    QLineEdit *m_leTicketNum = nullptr;
    QPushButton *m_btnSearch = nullptr;
    QLabel    *m_lblInfo = nullptr;

    // Rectification form (only enabled after a valid ENVIADA ticket is loaded)
    QGroupBox      *m_grpForm = nullptr;
    QComboBox      *m_cbInvoiceType = nullptr;     // R1, R2, R3, R4, R5
    QRadioButton   *m_rbDifferences = nullptr;     // I (default)
    QRadioButton   *m_rbSubstitution = nullptr;    // S
    QDateEdit      *m_deRectifyDate = nullptr;
    QLabel         *m_lblAmount = nullptr;         // label changes with mode
    QDoubleSpinBox *m_sbAmount = nullptr;          // corrected total (S) or delta (I), incl. IVA
    QLineEdit      *m_leDescription = nullptr;

    QPushButton *m_btnRectify = nullptr;
    QLabel      *m_lblResult = nullptr;

    // Loaded ticket state
    QString m_loadedTicket;
    // The ticket the in-flight rectificativa was sent for: its row, its reply and the
    // original it marks, whatever the window shows by then.
    QString m_submittedTicket;
    QString m_loadedCliente;
    double  m_loadedImporteTotal = 0.0; // includes IVA
    QString m_pendingRectifyId;         // empty when no AEAT call in flight

    // Captured at submit time so onVerifactuRequestFinished can persist correctly
    QString m_newInvoiceNumber;
    QDate   m_newInvoiceDate;
    double  m_submittedAmount = 0.0;    // signed for I, full corrected for S
    bool    m_submittedIsSubstitution = false;

    QSqlDatabase db;
};

#endif // RECTIFYINVOICEDIALOG_H
