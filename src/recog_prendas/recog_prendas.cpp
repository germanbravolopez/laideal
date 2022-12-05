#include "recog_prendas.h"
#include "ui_recog_prendas.h"
#include "sql_lite.h"

RecogPrendas::RecogPrendas(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::RecogPrendas)
{
    ui->setupUi(this);
    ui->le_search->setFocus();
}

RecogPrendas::~RecogPrendas()
{
    delete ui;
}

void RecogPrendas::on_pb_search_clicked()
{
    if (!ui->le_search->text().isNull())
    {
        QSqlQueryModel *model = new QSqlQueryModel;
        bool ok;
        ui->le_search->text().toUInt(&ok);
        if (ok)
        {
            if(ui->le_search->text().length() >= 9)
            {
                // Phone number
                QString client_from_tel_fijo = select_from_where_like(db, "nombre", "clientes", "tel_fijo", ui->le_search->text(), true);
                QString client_from_movil = select_from_where_like(db, "nombre", "clientes", "movil", ui->le_search->text(), true);
                if (!client_from_tel_fijo.isNull())
                {
                    db.open();
                    model->setQuery("SELECT n_recibo, cliente, fecha_recepcion, fecha_pago, fecha_recogida, importe, pagado, estado, cantidad, prenda, size, servicio, observaciones \
                                     FROM ingresos \
                                     WHERE cliente = '" + client_from_tel_fijo + "'");
                    db.close();
                }
                else if (!client_from_movil.isNull())
                {
                    db.open();
                    model->setQuery("SELECT n_recibo, cliente, fecha_recepcion, fecha_pago, fecha_recogida, importe, pagado, estado, cantidad, prenda, size, servicio, observaciones \
                                    FROM ingresos \
                                    WHERE cliente = '" + client_from_movil + "'");
                    db.close();
                }
            }
            else
            {
                // Ticket number
                db.open();
                model->setQuery("SELECT n_recibo, cliente, fecha_recepcion, fecha_pago, fecha_recogida, importe, pagado, estado, cantidad, prenda, size, servicio, observaciones \
                                FROM ingresos \
                                WHERE n_recibo = '" + ui->le_search->text() + "'");
                db.close();
            }
        }
        // Search is not a number
        else if (ui->le_search->text().isSimpleText())
        {
            // Text (client, address or another)
            QDate date_slash = QDate::fromString(ui->le_search->text(), "dd/MM/yyyy");
            QDate date_dash = QDate::fromString(ui->le_search->text(), "dd-MM-yyyy");
            if (!date_slash.isNull())
            {
                // Text is date
                db.open();
                model->setQuery("SELECT n_recibo, cliente, fecha_recepcion, fecha_pago, fecha_recogida, importe, pagado, estado, cantidad, prenda, size, servicio, observaciones \
                                FROM ingresos \
                                WHERE fecha_recepcion = '" + date_slash.toString("dd-MM-yyyy") + "' \
                                OR fecha_pago = '" + date_slash.toString("dd-MM-yyyy") + "' \
                                OR fecha_recogida = '" + date_slash.toString("dd-MM-yyyy") + "'");
                db.close();
            }
            else if (!date_dash.isNull())
            {
                // Text is date
                db.open();
                model->setQuery("SELECT n_recibo, cliente, fecha_recepcion, fecha_pago, fecha_recogida, importe, pagado, estado, cantidad, prenda, size, servicio, observaciones \
                                FROM ingresos \
                                WHERE fecha_recepcion = '" + date_dash.toString("dd-MM-yyyy") + "' \
                                OR fecha_pago = '" + date_dash.toString("dd-MM-yyyy") + "' \
                                OR fecha_recogida = '" + date_dash.toString("dd-MM-yyyy") + "'");
                db.close();
            }
            else
            {
                // Text is not a date
                db.open();
                model->setQuery("SELECT n_recibo, cliente, fecha_recepcion, fecha_pago, fecha_recogida, importe, pagado, estado, cantidad, prenda, size, servicio, observaciones \
                                FROM ingresos \
                                WHERE cliente like '%" + ui->le_search->text() + "%'");
                db.close();
            }
        }
        else
        {
            QMessageBox::question(this, tr("Búsqueda incorrecta"),
                                  tr("El contenido de la búsqueda no se ha identificado.\n"
                                  "Hablar con Germán..."),
                                  QMessageBox::Ok, QMessageBox::Ok);
            qDebug() << "Text is not identified!";
        }
        model->setHeaderData(0,  Qt::Horizontal, tr("n_recibo"));
        model->setHeaderData(1,  Qt::Horizontal, tr("cliente"));
        model->setHeaderData(2,  Qt::Horizontal, tr("fecha_recepcion"));
        model->setHeaderData(3,  Qt::Horizontal, tr("fecha_pago"));
        model->setHeaderData(4,  Qt::Horizontal, tr("fecha_recogida"));
        model->setHeaderData(5,  Qt::Horizontal, tr("importe"));
        model->setHeaderData(6,  Qt::Horizontal, tr("pagado"));
        model->setHeaderData(7,  Qt::Horizontal, tr("estado"));
        model->setHeaderData(8,  Qt::Horizontal, tr("cantidad"));
        model->setHeaderData(9,  Qt::Horizontal, tr("prenda"));
        model->setHeaderData(10, Qt::Horizontal, tr("size"));
        model->setHeaderData(11, Qt::Horizontal, tr("servicio"));
        model->setHeaderData(12, Qt::Horizontal, tr("observaciones"));
        ui->tableView->setModel(model);
        ui->tableView->resizeColumnsToContents();
        ui->tableView->sortByColumn(0, Qt::AscendingOrder);
    }
}
