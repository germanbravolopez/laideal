#include "facturas.h"
#include "ui_facturas.h"
#include "sql_lite.h"

Facturas::Facturas(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::Facturas)
{
    ui->setupUi(this);
    initial_settings();
}

Facturas::~Facturas()
{
    delete ui;
}

void Facturas::initial_settings()
{
    QStringList iva_list = {"21", "10", "0"};
    ui->cb_iva->addItems(iva_list);
    reset_all_contents();
}

void Facturas::reset_all_contents()
{
    ui->le_fra->clear();
    ui->de_fecha->setDate(QDate::currentDate());
    ui->cb_servicio->setCurrentText("");
    ui->le_producto->clear();
    ui->cb_empresa->setCurrentText("");
    ui->cb_iva->setCurrentText("21");
    ui->le_importe->clear();
    ui->le_base->clear();
    ui->le_iva->clear();
}

void Facturas::populate_empresas()
{
    QStringList empresas_names = read_column_from_table(db, "nombre", "proveedores", "");
    ui->cb_empresa->addItems(empresas_names);
    ui->cb_empresa->setCurrentText("");
}

void Facturas::populate_servicios()
{
    QStringList servicios_names = read_column_from_table(db, "nombre", "servicios", "");
    ui->cb_servicio->addItems(servicios_names);
    ui->cb_servicio->setCurrentText("");
}

bool Facturas::validate_form()
{
    bool ok = 0;
    // Avoid n_fra, service, company and cost to be empty
    if (ui->le_fra->text() != "" &&
            ui->cb_servicio->currentText() != "" &&
            ui->cb_empresa->currentText() != "" &&
            ui->le_importe->text() != "") {
        // Check current company as part of the company list
        if (ui->cb_empresa->findText(ui->cb_empresa->currentText(),Qt::MatchExactly) != -1) {
            if (read_lock_for_month_and_year(db, "gastos", ui->de_fecha->date().month(), ui->de_fecha->date().year()) == 0)
                ok = 1;
            else
                QMessageBox::warning(this, tr("Trimestre bloqueado"),
                                     tr("La fecha de la factura pertenece a un trimestre que se encuentra bloqueado por la contabilidad."),
                                     QMessageBox::Ok, QMessageBox::Ok);
        }
        else
            QMessageBox::warning(this, "Formulario factura",
                                 "El nombre de la empresa introducida no se encuentra en la lista de empresas.\n"
                                 "Añadirla en el listado de empresas antes de introducir esta factura.",
                                 QMessageBox::Ok, QMessageBox::Ok);
    }
    else
        QMessageBox::warning(this, "Formulario factura",
                             "Formulario incompleto.\n"
                             "Para poder guardar la factura, al menos es necesario rellenar los siguientes campos: Nº Fra., Servicio, Empresa e Importe.",
                             QMessageBox::Ok, QMessageBox::Ok);

    return ok;
}

void Facturas::save_factura()
{
    int id_max = read_max_value_in_column_from_table(db, "id", "gastos");
    db.open();
    QSqlQuery q;
    q.prepare("INSERT INTO gastos (id, n_factura, servicio, descripcion, empresa, fecha, iva, importe, edit_lock) "
              "VALUES (:id, :n_factura, :servicio, :descripcion, :empresa, :fecha, :iva, :importe, :edit_lock);");
    q.bindValue(":id", QString::number(id_max + 1));
    q.bindValue(":n_factura", ui->le_fra->text());
    q.bindValue(":servicio", ui->cb_servicio->currentText());
    q.bindValue(":descripcion", ui->le_producto->text());
    q.bindValue(":empresa", ui->cb_empresa->currentText());
    q.bindValue(":fecha", ui->de_fecha->date().toString("dd-MM-yyyy"));
    q.bindValue(":iva", ui->cb_iva->currentText());
    q.bindValue(":importe", ui->le_importe->text().replace(",","."));
    q.bindValue(":edit_lock", "0");
    q.exec();
    q.clear();
    db.close();
}

void Facturas::on_buttonBox_clicked(QAbstractButton *button)
{
    if (button == ui->buttonBox->button(QDialogButtonBox::Cancel))
        this->close();
    else if (button == ui->buttonBox->button(QDialogButtonBox::Reset))
        reset_all_contents();
    else if (button == ui->buttonBox->button(QDialogButtonBox::Save)) {
        if (validate_form()) {
            save_factura();
            reset_all_contents();
        }
    }
    else
        QMessageBox::critical(this, "Error en formulario factura",
                              "Boton no definido.",
                              QMessageBox::Ok, QMessageBox::Ok);
}

void Facturas::on_le_importe_textEdited(const QString &arg1)
{
    if (arg1.left(1) == "0"  || arg1.left(1) == "1" || arg1.left(1) == "2" || arg1.left(1) == "3"
             || arg1.left(1) == "4" || arg1.left(1) == "5" || arg1.left(1) == "6"
             || arg1.left(1) == "7" || arg1.left(1) == "8" || arg1.left(1) == "9") {
        ui->le_base->setText(QString::number(arg1.toFloat() / (1 + (ui->cb_iva->currentText().toFloat() / 100)), 'f', 2));
        ui->le_iva->setText(QString::number(arg1.toFloat() * (1 - 1 / (1 + (ui->cb_iva->currentText().toFloat() / 100))), 'f', 2));
    }
}

void Facturas::on_cb_iva_currentTextChanged(const QString &arg1)
{
    on_le_importe_textEdited(ui->le_importe->text());
}
