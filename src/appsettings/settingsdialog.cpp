#include "settingsdialog.h"
#include "appsettings.h"
#include <QTabWidget>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QDoubleValidator>

// Helpers — create a row: line edit + Browse button
static QWidget *browseRow(QLineEdit *le, bool directory, SettingsDialog *dlg)
{
    auto *w   = new QWidget;
    auto *hbl = new QHBoxLayout(w);
    hbl->setContentsMargins(0, 0, 0, 0);
    hbl->addWidget(le);
    auto *btn = new QPushButton(SettingsDialog::tr("Examinar..."));
    btn->setFixedWidth(90);
    hbl->addWidget(btn);
    QObject::connect(btn, &QPushButton::clicked, dlg, [le, directory, dlg]() {
        dlg->browsePath(le, directory);
    });
    return w;
}

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Configuración"));
    setMinimumWidth(560);

    auto *mainLayout = new QVBoxLayout(this);
    auto *tabs       = new QTabWidget(this);

    buildGeneralTab(tabs);
    buildReportsTab(tabs);
    buildBusinessTab(tabs);
    buildVerifactuTab(tabs);

    mainLayout->addWidget(tabs);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
    mainLayout->addWidget(buttons);
}

void SettingsDialog::buildGeneralTab(QTabWidget *tabs)
{
    AppSettings *s = AppSettings::instance();
    auto *w   = new QWidget;
    auto *fl  = new QFormLayout(w);
    fl->setRowWrapPolicy(QFormLayout::WrapLongRows);

    m_dbPath = new QLineEdit(s->dbPath());
    auto *dbRow = browseRow(m_dbPath, false, this);
    auto *dbNote = new QLabel(tr("<i>Requiere reiniciar la aplicación para aplicar.</i>"));
    dbNote->setStyleSheet("color: gray;");
    auto *dbLayout = new QVBoxLayout;
    dbLayout->setContentsMargins(0, 0, 0, 0);
    dbLayout->addWidget(dbRow);
    dbLayout->addWidget(dbNote);
    auto *dbContainer = new QWidget;
    dbContainer->setLayout(dbLayout);
    fl->addRow(tr("Base de datos (.db):"), dbContainer);

    m_iconPath = new QLineEdit(s->iconPath());
    fl->addRow(tr("Icono de la aplicación (.ico):"), browseRow(m_iconPath, false, this));

    m_ivaRate = new QLineEdit(QString::number(s->ivaRate(), 'f', 1));
    m_ivaRate->setValidator(new QDoubleValidator(0.0, 100.0, 2, m_ivaRate));
    m_ivaRate->setFixedWidth(80);
    fl->addRow(tr("Tipo de IVA (%):"), m_ivaRate);

    m_enablePrinting = new QCheckBox(tr("Habilitar impresión de tickets y facturas"));
    m_enablePrinting->setChecked(s->enablePrinting());
    fl->addRow(m_enablePrinting);

    auto *settingsPathLabel = new QLabel(
        tr("Archivo de configuración: <b>%1</b>").arg(s->filePath()));
    settingsPathLabel->setWordWrap(true);
    fl->addRow(settingsPathLabel);

    tabs->addTab(w, tr("General"));
}

void SettingsDialog::buildReportsTab(QTabWidget *tabs)
{
    AppSettings *s = AppSettings::instance();
    auto *w  = new QWidget;
    auto *fl = new QFormLayout(w);
    fl->setRowWrapPolicy(QFormLayout::WrapLongRows);

    m_contabilidadPath = new QLineEdit(s->contabilidadPath());
    fl->addRow(tr("Contabilidad (PDF):"), browseRow(m_contabilidadPath, true, this));

    m_listadosPrendasPath = new QLineEdit(s->listadosPrendasPath());
    fl->addRow(tr("Listado de prendas (PDF):"), browseRow(m_listadosPrendasPath, true, this));

    m_listadosGastosPath = new QLineEdit(s->listadosGastosPath());
    fl->addRow(tr("Listado de gastos (PDF):"), browseRow(m_listadosGastosPath, true, this));

    m_templatePath = new QLineEdit(s->printTemplatePath());
    fl->addRow(tr("Plantilla Excel tickets (.xlsx):"), browseRow(m_templatePath, false, this));

    m_scriptPath = new QLineEdit(s->printScriptPath());
    fl->addRow(tr("Script de impresión (.bat):"), browseRow(m_scriptPath, false, this));

    tabs->addTab(w, tr("Rutas de salida"));
}

void SettingsDialog::buildBusinessTab(QTabWidget *tabs)
{
    AppSettings *s = AppSettings::instance();
    auto *w  = new QWidget;
    auto *fl = new QFormLayout(w);

    m_businessName    = new QLineEdit(s->businessName());
    m_businessAddress = new QLineEdit(s->businessAddress());
    m_businessCity    = new QLineEdit(s->businessCity());

    fl->addRow(tr("Nombre:"),    m_businessName);
    fl->addRow(tr("Dirección:"), m_businessAddress);
    fl->addRow(tr("Ciudad:"),    m_businessCity);

    tabs->addTab(w, tr("Empresa"));
}

void SettingsDialog::buildVerifactuTab(QTabWidget *tabs)
{
    AppSettings *s = AppSettings::instance();
    auto *w  = new QWidget;
    auto *fl = new QFormLayout(w);

    m_vNif  = new QLineEdit(s->verifactuNif());
    m_vName = new QLineEdit(s->verifactuName());

    m_vKey  = new QLineEdit(s->verifactuServiceKey());
    m_vKey->setEchoMode(QLineEdit::Password);

    m_vProduction = new QCheckBox(tr("Entorno de PRODUCCIÓN (marcar solo para uso real con AEAT)"));
    m_vProduction->setChecked(s->verifactuProduction());

    fl->addRow(tr("NIF del emisor:"),     m_vNif);
    fl->addRow(tr("Nombre empresa:"),     m_vName);
    fl->addRow(tr("Clave de servicio:"),  m_vKey);
    fl->addRow(m_vProduction);

    auto *note = new QLabel(tr(
        "<i>La clave de servicio se obtiene en "
        "<a href='https://facturae.irenesolutions.com/verifactu/go'>"
        "facturae.irenesolutions.com</a>.</i>"));
    note->setOpenExternalLinks(true);
    note->setWordWrap(true);
    fl->addRow(note);

    tabs->addTab(w, tr("Verifactu"));
}

void SettingsDialog::accept()
{
    AppSettings *s = AppSettings::instance();

    s->setDbPath(m_dbPath->text().trimmed());
    s->setIconPath(m_iconPath->text().trimmed());
    s->setIvaRate(m_ivaRate->text().replace(',', '.').toDouble());
    s->setEnablePrinting(m_enablePrinting->isChecked());

    s->setContabilidadPath(m_contabilidadPath->text().trimmed());
    s->setListadosPrendasPath(m_listadosPrendasPath->text().trimmed());
    s->setListadosGastosPath(m_listadosGastosPath->text().trimmed());
    s->setPrintTemplatePath(m_templatePath->text().trimmed());
    s->setPrintScriptPath(m_scriptPath->text().trimmed());

    s->setBusinessName(m_businessName->text().trimmed());
    s->setBusinessAddress(m_businessAddress->text().trimmed());
    s->setBusinessCity(m_businessCity->text().trimmed());

    s->setVerifactuNif(m_vNif->text().trimmed());
    s->setVerifactuName(m_vName->text().trimmed());
    s->setVerifactuServiceKey(m_vKey->text().trimmed());
    s->setVerifactuProduction(m_vProduction->isChecked());

    if (!s->save())
        QMessageBox::warning(this, tr("Error"),
            tr("No se pudo guardar la configuración en:\n%1").arg(s->filePath()));

    QDialog::accept();
}

void SettingsDialog::browsePath(QLineEdit *target, bool directory)
{
    QString path;
    if (directory)
        path = QFileDialog::getExistingDirectory(this, tr("Seleccionar carpeta"),
                                                 target->text());
    else
        path = QFileDialog::getOpenFileName(this, tr("Seleccionar archivo"),
                                            target->text());
    if (!path.isEmpty())
        target->setText(path);
}
