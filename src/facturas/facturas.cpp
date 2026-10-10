#include "facturas.h"
#include "sql_lite.h"
#include "uikit.h"

#include <QComboBox>
#include <QDateEdit>
#include <QDebug>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSqlError>
#include <QSqlQuery>
#include <QVBoxLayout>

Facturas::Facturas(const QSqlDatabase &database, QWidget *parent) :
    QDialog(parent),
    db(database)
{
    setAttribute(Qt::WA_DeleteOnClose);
    UiKit::setUpDialog(this, "Formulario de facturas de gastos");
    buildUi();
    resetAllContents();
    m_lblResult->showInfo("Rellene la factura y pulse \"Guardar factura\".");
}

void Facturas::buildUi()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(UiKit::introPanel(
        "Registra una factura de gastos (compras a proveedores). El importe se introduce con el "
        "IVA incluido; la base imponible y la cuota de IVA se calculan solas."));

    QGroupBox *grpInvoice = new QGroupBox("Factura");
    QFormLayout *invoiceForm = new QFormLayout(grpInvoice);
    m_leFra = new QLineEdit();
    m_leFra->setObjectName("leFra");     // stable names for the e2e test bench
    m_leFra->setPlaceholderText("Número de la factura del proveedor");
    invoiceForm->addRow("Nº factura:", m_leFra);
    m_deFecha = new QDateEdit();
    m_deFecha->setObjectName("deFecha");
    m_deFecha->setCalendarPopup(true);
    m_deFecha->setDisplayFormat("dd-MM-yyyy");
    invoiceForm->addRow("Fecha:", m_deFecha);
    m_cbEmpresa = new QComboBox();
    m_cbEmpresa->setObjectName("cbEmpresa");
    m_cbEmpresa->setEditable(true);
    invoiceForm->addRow("Empresa:", m_cbEmpresa);
    m_cbServicio = new QComboBox();
    m_cbServicio->setObjectName("cbServicio");
    m_cbServicio->setEditable(true);
    invoiceForm->addRow("Servicio:", m_cbServicio);
    m_leDescripcion = new QLineEdit();
    m_leDescripcion->setObjectName("leDescripcion");
    m_leDescripcion->setPlaceholderText("Opcional");
    invoiceForm->addRow("Descripción:", m_leDescripcion);
    layout->addWidget(grpInvoice);

    QGroupBox *grpAmount = new QGroupBox("Importe");
    QFormLayout *amountForm = new QFormLayout(grpAmount);
    amountForm->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    m_cbIva = new QComboBox();
    m_cbIva->setObjectName("cbIva");
    for (const char *rate : { "21", "10", "0" })
        m_cbIva->addItem(QString(rate) + " %", QString(rate));
    amountForm->addRow("Tipo de IVA:", m_cbIva);
    m_leImporte = new QLineEdit();
    m_leImporte->setObjectName("leImporte");
    m_leImporte->setPlaceholderText("0,00");
    m_leImporte->setMinimumWidth(140);
    amountForm->addRow("Importe total (IVA incl.):", m_leImporte);
    m_leBase = new QLineEdit();
    m_leBase->setObjectName("leBase");
    m_leBase->setReadOnly(true);
    m_leBase->setMinimumWidth(140);
    amountForm->addRow("Base imponible:", m_leBase);
    m_leIva = new QLineEdit();
    m_leIva->setObjectName("leIva");
    m_leIva->setReadOnly(true);
    m_leIva->setMinimumWidth(140);
    amountForm->addRow("Cuota de IVA:", m_leIva);
    layout->addWidget(grpAmount);

    QHBoxLayout *actions = new QHBoxLayout();
    QPushButton *btnReset = UiKit::secondaryButton("Limpiar formulario", "btnReset");
    actions->addWidget(btnReset);
    actions->addStretch();
    QPushButton *btnSave = UiKit::primaryButton("Guardar factura", "btnSave");
    actions->addWidget(btnSave);
    layout->addLayout(actions);

    m_lblResult = new UiKit::ResultPanel();
    layout->addWidget(m_lblResult);
    layout->addLayout(UiKit::closeRow(this));

    connect(btnSave,  &QPushButton::clicked, this, &Facturas::onSaveClicked);
    connect(btnReset, &QPushButton::clicked, this, [this]() {
        resetAllContents();
        m_lblResult->showInfo("Formulario limpio.");
    });
    connect(m_leImporte, &QLineEdit::textEdited, this, &Facturas::updateTaxSplit);
    connect(m_cbIva, qOverload<int>(&QComboBox::currentIndexChanged), this, &Facturas::updateTaxSplit);
}

void Facturas::resetAllContents()
{
    m_leFra->clear();
    m_deFecha->setDate(QDate::currentDate());
    m_cbServicio->setCurrentText("");
    m_leDescripcion->clear();
    m_cbEmpresa->setCurrentText("");
    m_cbIva->setCurrentIndex(0);   // 21 %
    m_leImporte->clear();
    m_leBase->clear();
    m_leIva->clear();
    m_leFra->setFocus();
}

void Facturas::populateEmpresas()
{
    m_cbEmpresa->addItems(readColumnFromTable(db, "nombre", "proveedores", ""));
    m_cbEmpresa->setCurrentText("");
}

void Facturas::populateServicios()
{
    m_cbServicio->addItems(readColumnFromTable(db, "nombre", "servicios", ""));
    m_cbServicio->setCurrentText("");
}

QString Facturas::validationError()
{
    if (m_leFra->text().trimmed().isEmpty() || m_cbServicio->currentText().isEmpty()
            || m_cbEmpresa->currentText().isEmpty() || m_leImporte->text().trimmed().isEmpty())
        return UiKit::errorHtml("Formulario incompleto.")
               + "<br>Para guardar la factura rellene al menos: Nº factura, Empresa, Servicio e Importe total.";
    bool isNumber = false;
    m_leImporte->text().trimmed().replace(',', '.').toDouble(&isNumber);
    if (!isNumber)
        return UiKit::errorHtml("El importe total no es un número válido.");
    if (m_cbEmpresa->findText(m_cbEmpresa->currentText(), Qt::MatchExactly) == -1)
        return UiKit::errorHtml("La empresa no está en la lista de proveedores.")
               + "<br>Añádala en Listado de proveedores antes de introducir esta factura.";
    // The whole quarter, both tables: a month without gastos in a closed quarter is closed too.
    if (quarterIsClosed(db, m_deFecha->date()))
        return UiKit::errorHtml("Trimestre bloqueado.")
               + "<br>La fecha de la factura pertenece a un trimestre cerrado por la contabilidad.";
    return QString();
}

void Facturas::onSaveClicked()
{
    const QString error = validationError();
    if (!error.isEmpty()) {
        m_lblResult->setText(error);
        return;
    }
    const QString summary = QString("Factura %1 de %2 guardada (%3 €, IVA %4 %).")
                                .arg(m_leFra->text().trimmed().toHtmlEscaped(),
                                     m_cbEmpresa->currentText().toHtmlEscaped(),
                                     moneyText(m_leImporte->text()).replace('.', ','),
                                     m_cbIva->currentData().toString());
    if (!saveFactura()) {
        m_lblResult->setText(UiKit::errorHtml("No se pudo guardar la factura.")
                             + "<br>Consulte el log (Ayuda → Mostrar log).");
        return;
    }
    resetAllContents();
    m_lblResult->setText(UiKit::okHtml(summary) + "<br>El formulario está listo para la siguiente.");
}

bool Facturas::saveFactura()
{
    const int idMax = readMaxValueInColumnFromTable(db, "id", "gastos");
    qDebug() << "saveFactura: INSERT INTO gastos id=" << (idMax + 1)
             << "n_factura=" << m_leFra->text()
             << "empresa=" << m_cbEmpresa->currentText()
             << "fecha=" << m_deFecha->date().toString("dd-MM-yyyy")
             << "iva=" << m_cbIva->currentData().toString()
             << "importe=" << m_leImporte->text();
    db.open();
    QSqlQuery q(db);
    q.prepare("INSERT INTO gastos (id, n_factura, servicio, descripcion, empresa, fecha, iva, importe, edit_lock) "
              "VALUES (:id, :n_factura, :servicio, :descripcion, :empresa, :fecha, :iva, :importe, :edit_lock);");
    q.bindValue(":id", QString::number(idMax + 1));
    q.bindValue(":n_factura", m_leFra->text().trimmed());
    q.bindValue(":servicio", m_cbServicio->currentText());
    q.bindValue(":descripcion", m_leDescripcion->text());
    q.bindValue(":empresa", m_cbEmpresa->currentText());
    q.bindValue(":fecha", m_deFecha->date().toString("dd-MM-yyyy"));
    q.bindValue(":iva", m_cbIva->currentData().toString());
    q.bindValue(":importe", moneyText(m_leImporte->text()));
    q.bindValue(":edit_lock", "0");
    const bool ok = q.exec();
    if (!ok)
        qWarning() << "saveFactura INSERT failed for" << m_leFra->text() << "-" << q.lastError().text();
    db.close();
    return ok;
}

double Facturas::taxBaseFromGross(double gross, double ivaRate)
{
    return gross / (1.0 + ivaRate / 100.0);
}

double Facturas::taxAmountFromGross(double gross, double ivaRate)
{
    return gross * (1.0 - 1.0 / (1.0 + ivaRate / 100.0));
}

void Facturas::updateTaxSplit()
{
    bool isNumber = false;
    const double gross = m_leImporte->text().trimmed().replace(',', '.').toDouble(&isNumber);
    if (!isNumber) {
        m_leBase->clear();
        m_leIva->clear();
        return;
    }
    const double iva = m_cbIva->currentData().toDouble();
    m_leBase->setText(QString::number(taxBaseFromGross(gross, iva), 'f', 2));
    m_leIva->setText(QString::number(taxAmountFromGross(gross, iva), 'f', 2));
}
