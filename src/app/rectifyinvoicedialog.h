#ifndef RECTIFYINVOICEDIALOG_H
#define RECTIFYINVOICEDIALOG_H

#include <QDialog>
#include <QSqlDatabase>
#include <QDate>
#include "verifactuintegration.h"

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
// submit to AEAT asynchronously, persist a new linked ingresos row on success
// and (for sustitucion) mark the original rows as RECTIFICADA. Art. 8.2.a
// RD 1007/2023 - corrections must use anulacion or rectificativa.
class RectifyInvoiceDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RectifyInvoiceDialog(QWidget *parent = nullptr);

    QSqlDatabase db;
    VerifactuIntegration *m_verifactu = nullptr;

private slots:
    void onSearchClicked();
    void onRectifyClicked();
    void onRectificationModeChanged();
    void onVerifactuRequestFinished(const QString &requestId, const VerifactuResult &result);

private:
    void setFormEnabled(bool enabled);
    // Inserts the rectificativa row at submit time with estado=PENDIENTE so the
    // n_recibo is claimed locally before the AEAT round-trip - prevents a regular
    // MainWindow::saveTicket from re-using the same number while we wait.
    void insertPlaceholderRow();
    // Patches the row inserted by insertPlaceholderRow with the AEAT reply: csv /
    // xml / hash / url + estado=ENVIADA on success, or estado=ERROR + description
    // on failure. For substitution-mode success it also marks the original rows
    // RECTIFICADA.
    void applyRectificationResult(const VerifactuResult &result);

    // Top row - ticket lookup
    QLineEdit *m_leTicketNum;
    QLabel    *m_lblInfo;

    // Rectification form (only enabled after a valid ENVIADA ticket is loaded)
    QGroupBox      *m_grpForm;
    QComboBox      *m_cbInvoiceType;     // R1, R2, R3, R4, R5
    QRadioButton   *m_rbDifferences;     // I (default)
    QRadioButton   *m_rbSubstitution;    // S
    QDateEdit      *m_deRectifyDate;
    QLabel         *m_lblAmount;         // label changes with mode
    QDoubleSpinBox *m_sbAmount;          // corrected total (S) or delta (I), incl. IVA
    QLineEdit      *m_leDescription;

    QPushButton *m_btnRectify;
    QLabel      *m_lblResult;

    // Loaded ticket state
    QString m_loadedTicket;
    QString m_loadedCliente;
    double  m_loadedImporteTotal = 0.0; // includes IVA
    QString m_loadedCSV;
    QString m_pendingRectifyId;         // empty when no AEAT call in flight

    // Captured at submit time so onVerifactuRequestFinished can persist correctly
    QString m_newInvoiceNumber;
    QDate   m_newInvoiceDate;
    double  m_submittedAmount = 0.0;    // signed for I, full corrected for S
    bool    m_submittedIsSubstitution = false;
};

#endif // RECTIFYINVOICEDIALOG_H
