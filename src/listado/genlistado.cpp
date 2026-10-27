#include "genlistado.h"
#include "sql_lite.h"
#include "mysortfilterproxymodel.h"
#include "appsettings.h"
#include "reporthtml.h"
#include "uikit.h"

#include <algorithm>
#include <numeric>

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
    if (max_year <= 0 || min_year <= 0)   // no gastos yet
        max_year = min_year = QDate::currentDate().year();
    QStringList fechas_list;
    int mid_year = max_year;
    while (mid_year >= min_year) {
        fechas_list.append(QString::number(mid_year));
        mid_year--;
    }
    m_cbYear->addItems(fechas_list);
}

bool GenListado::print_table()
{
    if (!model)
        return false;
    const QString html_table_prendas = generate_html_prendas_table(rowsSortedBy(model, LIST_PRENDAS_IDX_NAME, -1));
    // set path and print table
    QString path = AppSettings::instance()->listadosPrendasPath();
    QString filename = "/listado_prendas_" +
            QDate::currentDate().toString("yyyy-MM-dd") +
            ".pdf";
    // create directory in case it does not exists
    if (!QFile::exists(path))
        QDir().mkpath(path);
    m_lastPrintedFile = path + filename;
    return ReportHtml::writePdf(m_lastPrintedFile, html_table_prendas);
}

QVector<int> GenListado::rowsSortedBy(const QAbstractItemModel *model, int column, int dateColumn)
{
    QVector<int> rows(model->rowCount());
    std::iota(rows.begin(), rows.end(), 0);
    std::stable_sort(rows.begin(), rows.end(), [=](int a, int b) {
        const int byText = QString::localeAwareCompare(model->index(a, column).data().toString(),
                                                       model->index(b, column).data().toString());
        if (byText != 0 || dateColumn < 0)
            return byText < 0;
        return MySortFilterProxyModel::parseFecha(model->index(a, dateColumn).data().toString())
               < MySortFilterProxyModel::parseFecha(model->index(b, dateColumn).data().toString());
    });
    return rows;
}

QString GenListado::supplierTotalCells(qint64 totalCents, bool withClosedColumn)
{
    return "<td colspan='6' style='text-align:right; font-weight:bold;'>IMPORTE TOTAL:</td>"
           "<td colspan='" + QString::number(withClosedColumn ? 2 : 1) + "' style='text-align:right; font-weight:bold;'>"
           + ReportHtml::formatEuro(totalCents / 100.0) + "</td></tr>";
}

QString GenListado::rowYear(const QString &fecha)
{
    return fecha.trimmed().mid(6, 4);
}

QString GenListado::generate_html_prendas_table(const QVector<int> &rows)
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
    for (const int row : rows) {
        // set background color for even rows
        if (row_printed % 2 == 0)
            html_table += "<tr>";
        else
            html_table += "<tr style='background-color: #f6f7f9;'>";
        // set row content
        html_table += "<td>" + model->index(row, 0).data().toString().toHtmlEscaped() + "</td>";
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
    if (!model)
        return QString();
    // Grouped by supplier (then by date) without touching the order of the user's table.
    if (m_cbGroup->currentText() == C_PROVEEDORES)
        return generate_html_gastos_table(rowsSortedBy(model, GASTOS_IDX_CLIENT, GASTOS_IDX_FECHA));
    QVector<int> rows(model->rowCount());
    std::iota(rows.begin(), rows.end(), 0);
    return generate_html_gastos_table(rows);
}

bool GenListado::rowClosed(int row) const
{
    const QDate fecha = MySortFilterProxyModel::parseFecha(model->index(row, GASTOS_IDX_FECHA).data().toString());
    return m_closedQuarters.contains(quarterKey(fecha));
}

QString GenListado::generate_html_gastos_table(const QVector<int> &rows)
{
    m_closedQuarters = closedQuarterKeys(db);
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
    QString client, client_prev_row;
    qint64 importe_tot = 0;   // cents
    const bool withClosedColumn = m_cbType->currentText() == C_INCL_TODOS;
    for (const int row : rows) {
        // print row based on previous analysis
        if (check_years_invoice_type_for_row(row)) {
            const GastoAmounts amounts = splitGasto(model->index(row, 7).data().toString(),
                                                    model->index(row, 6).data().toString());

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
                    html_table += supplierTotalCells(importe_tot, withClosedColumn);
                    importe_tot = 0;
                    row_printed++;
                }
                importe_tot += amounts.importeCents;
                client_prev_row = client;
            }
            // set background color for even rows
            if (row_printed % 2 == 0)
                html_table += "<tr>";
            else
                html_table += "<tr style='background-color: #f6f7f9;'>";
            // set row content
            html_table +="<td>" + model->index(row, 1).data().toString().toHtmlEscaped() + "</td>"
                                "<td>" + model->index(row, 2).data().toString().toHtmlEscaped() + "</td>"
                                "<td>" + model->index(row, 4).data().toString().toHtmlEscaped() + "</td>"
                                "<td>" + model->index(row, 5).data().toString().toHtmlEscaped() + "</td>"
                                "<td style='text-align:right;'>" + ReportHtml::formatEuro(amounts.ivaCents / 100.0) + "</td>"
                                "<td style='text-align:right;'>" + ReportHtml::formatEuro(amounts.baseCents / 100.0) + "</td>"
                                "<td style='text-align:right;'>" + ReportHtml::formatEuro(amounts.importeCents / 100.0) + "</td>";
            if (m_cbType->currentText() == C_INCL_TODOS) {
                if (rowClosed(row))
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
        html_table += supplierTotalCells(importe_tot, withClosedColumn);
    }
    html_table += "</tbody></table>" + ReportHtml::documentClose();
    return row_printed == 0 ? QString() : html_table;
}

GenListado::GastoAmounts GenListado::splitGasto(const QString &importe, const QString &iva)
{
    double amount = 0.0;
    moneyParse(importe, &amount);
    double rate = 0.0;
    if (!moneyParse(iva, &rate) || rate <= 0.0)
        rate = 0.0;
    GastoAmounts result;
    result.importeCents = qRound64(amount * 100.0);
    result.baseCents = rate > 0.0 ? qRound64(result.importeCents / (1.0 + rate / 100.0))
                                  : result.importeCents;
    result.ivaCents = result.importeCents - result.baseCents;
    return result;
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
    return shouldPrintGastoRow(
        m_chkAllYears->isChecked(),
        m_cbYear->currentText(),
        rowYear(model->index(row, GASTOS_IDX_FECHA).data().toString()),
        m_cbType->currentText() == C_CONTAB_CERR,
        rowClosed(row));
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
    if (html.isEmpty()) {
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
