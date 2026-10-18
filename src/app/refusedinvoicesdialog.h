#ifndef REFUSEDINVOICESDIALOG_H
#define REFUSEDINVOICESDIALOG_H

// Facturas rechazadas: the invoices behind the red status-bar count (sql_lite
// refusedInvoices), with the ticket, payment date, amount and AEAT's reason, so each one
// can be found and resent. "Abrir en Recogida" (or a double click) asks MainWindow to
// open Recogida de Prendas on the selected ticket.

#include <QDialog>
#include <QSqlDatabase>

#include "sql_lite.h"

class QTableWidget;
class QPushButton;
namespace UiKit { class ResultPanel; }

class RefusedInvoicesDialog : public QDialog
{
    Q_OBJECT

public:
    RefusedInvoicesDialog(const QSqlDatabase &database, const QString &floorIso, QWidget *parent = nullptr);
    void reload();

signals:
    void openTicket(const QString &nRecibo, int seq);

private:
    void openSelected();

    QSqlDatabase db;
    QString m_floorIso;
    QVector<RefusedInvoice> m_invoices;
    QTableWidget *m_table = nullptr;
    QPushButton *m_btnOpen = nullptr;
    UiKit::ResultPanel *m_result = nullptr;
};

#endif // REFUSEDINVOICESDIALOG_H
