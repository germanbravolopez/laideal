#ifndef CANCELINVOICEDIALOG_H
#define CANCELINVOICEDIALOG_H

#include <QDialog>
#include <QSqlDatabase>
#include <QDate>
#include "verifactuintegration.h"

class QLineEdit;
class QLabel;
class QPushButton;

class CancelInvoiceDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CancelInvoiceDialog(QWidget *parent = nullptr);

    QSqlDatabase db;
    VerifactuIntegration *m_verifactu = nullptr;

private slots:
    void onSearchClicked();
    void onCancelClicked();

private:
    QLineEdit   *m_leTicketNum;
    QLabel      *m_lblInfo;
    QPushButton *m_btnCancel;
    QLabel      *m_lblResult;

    QString m_loadedTicket;
    QDate   m_loadedDate;
    QString m_loadedCSV;
};

#endif // CANCELINVOICEDIALOG_H
