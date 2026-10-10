#include "genlistado.h"
#include "sql_lite.h"
#include "mysortfilterproxymodel.h"
#include "appsettings.h"
#include "reporthtml.h"
#include "uikit.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

GenListado::GenListado(const QSqlDatabase &database, QWidget *parent) :
    QDialog(parent),
    db(database)
{
    UiKit::setUpDialog(this, "Generar listado de gastos en PDF", 480);
    buildUi();
}

void GenListado::buildUi()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(UiKit::introPanel(
        "Genera en PDF el listado de las facturas de gastos de un año (o de todos), agrupado "
        "por fecha o por proveedor."));

    QGroupBox *grpFilters = new QGroupBox("Listado");
    QFormLayout *form = new QFormLayout(grpFilters);
    form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    m_cbYear = new QComboBox();
    m_cbYear->setObjectName("cbYear");     // stable names for the e2e test bench
    m_cbYear->setMinimumWidth(110);
    m_chkAllYears = new QCheckBox("Todos los años");
    m_chkAllYears->setObjectName("chkAllYears");
    QHBoxLayout *yearRow = new QHBoxLayout();
    yearRow->addWidget(m_cbYear);
    yearRow->addWidget(m_chkAllYears);
    yearRow->addStretch();
    form->addRow("Año:", yearRow);
    m_cbType = new QComboBox();
    m_cbType->setObjectName("cbType");
    m_cbType->addItems({C_INCL_TODOS, C_CONTAB_CERR});
    form->addRow("Gastos:", m_cbType);
    m_cbGroup = new QComboBox();
    m_cbGroup->setObjectName("cbGroup");
    m_cbGroup->addItems({C_FECHAS, C_PROVEEDORES});
    form->addRow("Agrupar por:", m_cbGroup);
    layout->addWidget(grpFilters);

    QHBoxLayout *actions = new QHBoxLayout();
    actions->addStretch();
    QPushButton *btnGenerate = UiKit::primaryButton("Generar listado PDF", "btnGenerate");
    actions->addWidget(btnGenerate);
    layout->addLayout(actions);
    m_lblResult = new UiKit::ResultPanel("Elija el año y pulse \"Generar listado PDF\".");
    layout->addWidget(m_lblResult);
    layout->addLayout(UiKit::closeRow(this));

    set_cb_fechas();
    connect(m_chkAllYears, &QCheckBox::toggled, m_cbYear, &QWidget::setDisabled);
    connect(btnGenerate, &QPushButton::clicked, this, &GenListado::onGenerateClicked);
}

void GenListado::set_cb_fechas()
{
    int max_year = readMaxNMinYearInColumnFromTable(db, true, "fecha", "gastos");
    int min_year = readMaxNMinYearInColumnFromTable(db, false, "fecha", "gastos");
    QStringList fechas_list;
    int mid_year = max_year;
    while (mid_year >= min_year) {
        fechas_list.append(QString::number(mid_year));
        mid_year--;
    }
    m_cbYear->addItems(fechas_list);
}

void GenListado::print_table()
{
    model->sort(0, Qt::AscendingOrder);
    QString html_table_prendas = generate_html_prendas_table();
    // set path and print table
    QString path = AppSettings::instance()->listadosPrendasPath();
    QString filename = "/listado_prendas_" +
            QDate::currentDate().toString("yyyy-MM-dd") +
            ".pdf";
    // create directory in case it does not exists
    if (!QFile::exists(path))
        QDir().mkpath(path);
    ReportHtml::writePdf(path + filename, html_table_prendas);
}

QString GenListado::generate_html_prendas_table()
{
    QString html_table = ReportHtml::documentOpen("Listado de Prendas")
            + ReportHtml::tableOpen() +
                "<thead>"
                    "<tr>"
                        "<th>Nombre</th>"
                        "<th style='text-align:right;'>Precio Limpieza</th>"
                        "<th style='text-align:right;'>Precio Plancha</th>"
                    "</tr>"
                "</thead>"
                "<tbody>";

    // add each line of data
    int row_printed = 0;
    for (int row = 0; row < model->rowCount(); row++) {
        // set background color for even rows
        if (row_printed % 2 == 0)
            html_table += "<tr>";
        else
            html_table += "<tr style='background-color: #f6f7f9;'>";
        // set row content
        html_table += "<td>" + model->index(row, 0).data().toString() + "</td>";
        if (model->index(row, 1).data().toFloat() == 0.0)
            html_table += "<td></td>";
        else
            html_table += "<td style='text-align:right;'>"
                    + ReportHtml::formatEuro(model->index(row, 1).data().toFloat()) + "</td>";
        if (model->index(row, 2).data().toFloat() == 0.0)
            html_table += "<td></td>";
        else
            html_table += "<td style='text-align:right;'>"
                    + ReportHtml::formatEuro(model->index(row, 2).data().toFloat()) + "</td>";
        html_table += "</tr>";
        row_printed++;
    }
    html_table += "</tbody></table>" + ReportHtml::documentClose();
    return html_table;
}


QString GenListado::generate_html_gastos_table_with_specific_conditions()
{
    // agrupar por proveedores if selected
    if (m_cbGroup->currentText() == C_PROVEEDORES)
        model->sort(GASTOS_IDX_CLIENT, Qt::AscendingOrder);
    return generate_html_gastos_table();
}

QString GenListado::generate_html_gastos_table()
{
    QString html_table = ReportHtml::documentOpen("Listado de Gastos")
            + ReportHtml::tableOpen() +
                "<thead>"
                    "<tr>"
                        "<th>N. Fra</th>"
                        "<th>Servicio</th>"
                        "<th>Empresa</th>"
                        "<th>Fecha</th>"
                        "<th style='text-align:right;'>IVA</th>"
                        "<th style='text-align:right;'>Base</th>"
                        "<th style='text-align:right;'>Importe</th>";
    if (m_cbType->currentText() == C_INCL_TODOS)
        html_table += "<th>Cerrado por contabilidad</th>";
    html_table += "</tr>" "</thead>" "<tbody>";

    // add each line of data
    int row_printed = 0;
    QString client, client_prev_row, importe, iva, base;
    float importe_tot = 0.0;
    for (int row = 0; row < model->rowCount(); row++) {
        // print row based on previous analysis
        if (check_years_invoice_type_for_row(row)) {
            // calculate iva and base
            importe = QString::number(model->index(row, 7).data().toFloat(), 'f', 2);
            if (model->index(row, 6).data().toString() == "21" || model->index(row, 6).data().toString() == "10")
                base = QString::number(importe.toFloat() / (1 + (model->index(row, 6).data().toFloat() / 100)), 'f', 2);
            else
                base = QString::number(importe.toFloat(), 'f', 2);
            iva = QString::number(importe.toFloat() - base.toFloat(), 'f', 2);

            // generate new row if group per clients
            if (m_cbGroup->currentText() == C_PROVEEDORES) {
                client = model->index(row, 4).data().toString();
                if (client != client_prev_row && row_printed != 0) {
                    // set background color for even rows
                    if (row_printed % 2 == 0)
                        html_table += "<tr>";
                    else
                        html_table += "<tr style='background-color: #f6f7f9;'>";
                    // set total costs row
                    html_table +="<td colspan='6' style='text-align:right; font-weight:bold;'>IMPORTE TOTAL:</td>"
                                        "<td colspan='2' style='text-align:right; font-weight:bold;'>" + ReportHtml::formatEuro(importe_tot) +
                                        "</td></tr>";
                    importe_tot = 0.0;
                    row_printed++;
                }
                importe_tot += importe.toFloat();
                client_prev_row = client;
            }
            // set background color for even rows
            if (row_printed % 2 == 0)
                html_table += "<tr>";
            else
                html_table += "<tr style='background-color: #f6f7f9;'>";
            // set row content
            html_table +="<td>" + model->index(row, 1).data().toString() + "</td>"
                                "<td>" + model->index(row, 2).data().toString() + "</td>"
                                "<td>" + model->index(row, 4).data().toString() + "</td>"
                                "<td>" + model->index(row, 5).data().toString() + "</td>"
                                "<td style='text-align:right;'>" + ReportHtml::formatEuro(iva.toFloat()) + "</td>"
                                "<td style='text-align:right;'>" + ReportHtml::formatEuro(base.toFloat()) + "</td>"
                                "<td style='text-align:right;'>" + ReportHtml::formatEuro(importe.toFloat()) + "</td>";
            if (m_cbType->currentText() == C_INCL_TODOS) {
                if (model->index(row, 8).data().toBool())
                    html_table += "<td>Si</td>";
                else
                    html_table += "<td>No</td>";
            }
            html_table += "</tr>";
            row_printed++;
        }
    }
    // generate new row for last group of clients
    if (m_cbGroup->currentText() == C_PROVEEDORES) {
        // set background color for even rows
        if (row_printed % 2 == 0)
            html_table += "<tr>";
        else
            html_table += "<tr style='background-color: #f6f7f9;'>";
        // set total costs row
        html_table +="<td colspan='6' style='text-align:right; font-weight:bold;'>IMPORTE TOTAL:</td>"
                            "<td colspan='2' style='text-align:right; font-weight:bold;'>" + ReportHtml::formatEuro(importe_tot) +
                            "</td></tr>";
    }
    html_table += "</tbody></table>" + ReportHtml::documentClose();
    if (row_printed == 0)
        return C_NO_ROWS;
    else
        return html_table;
}

bool GenListado::shouldPrintGastoRow(bool allYears, const QString &selectedYear,
                                     const QString &rowYear, bool onlyClosed, bool rowClosed)
{
    const bool dateOk   = allYears || (selectedYear == rowYear);
    const bool contabOk = !onlyClosed || rowClosed;
    return dateOk && contabOk;
}

bool GenListado::check_years_invoice_type_for_row(int row)
{
    // rowYear = the yyyy in the dd-MM-yyyy date string (chars 6..9).
    return shouldPrintGastoRow(
        m_chkAllYears->isChecked(),
        m_cbYear->currentText(),
        model->index(row, GASTOS_IDX_FECHA).data().toString().mid(6, 4),
        m_cbType->currentText() == C_CONTAB_CERR,
        model->index(row, GASTOS_IDX_CONTAB).data().toBool());
}

QString GenListado::filenameSuffix(const QString &agrupar, const QString &tipoGastos,
                                   bool allYears, const QString &selectedYear)
{
    QString suffix;
    suffix += "agrupado_" + agrupar.toLower().replace(" ", "_") + "_";
    suffix += tipoGastos.toLower().replace(" ", "_") + "_";
    suffix += allYears ? QStringLiteral("todos_los_años") : selectedYear;
    return suffix;
}

QString GenListado::add_suffix_to_filename()
{
    return filenameSuffix(m_cbGroup->currentText(), m_cbType->currentText(),
                          m_chkAllYears->isChecked(), m_cbYear->currentText());
}

void GenListado::onGenerateClicked()
{
    const QString html = generate_html_gastos_table_with_specific_conditions();
    if (html == C_NO_ROWS) {
        qWarning() << "GenListado: no gastos rows for the selected configuration";
        m_lblResult->setText(UiKit::warnHtml("No hay gastos para la configuración elegida.")
                             + "<br>Revise el año y el tipo de gastos.");
        return;
    }
    const QString path = AppSettings::instance()->listadosGastosPath();
    const QString file = path + "/listado_gastos_" + QDate::currentDate().toString("yyyy-MM-dd_")
                         + add_suffix_to_filename() + ".pdf";
    QDir().mkpath(path);
    // Always regenerated: the gastos may have changed since an earlier listado today.
    if (!ReportHtml::writePdf(file, html)) {
        m_lblResult->setText(UiKit::errorHtml("No se pudo escribir el PDF.") + "<br>" + file.toHtmlEscaped());
        return;
    }
    m_lblResult->setText(UiKit::okHtml("Listado de gastos generado.") + "<br>" + UiKit::fileLinkHtml(file));
}
