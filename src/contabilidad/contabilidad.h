#ifndef CONTABILIDAD_H
#define CONTABILIDAD_H

#include <QMainWindow>
#include <QMessageBox>
#include <QSqlQueryModel>
#include <QMessageBox>
#include <QDesktopServices>
#include <QDir>

#define C_MENSUAL    "Mensual"
#define C_TRIMESTRAL "Trimestral"
#define C_ANUAL      "Anual"

namespace Ui {
class Contabilidad;
}

class Contabilidad : public QMainWindow
{
    Q_OBJECT

public:
    explicit Contabilidad(QWidget *parent = nullptr);
    ~Contabilidad();
    QSqlDatabase db;
    bool revertirOn = false;
    void resetAllContents();

private slots:
    void initialSettings();

    void on_bb_ok_cancel_accepted();
    void on_bb_ok_cancel_rejected();
    void on_cb_config_currentTextChanged(const QString &arg1);

    void generateContabilidad();
    float getTotalIncome(QString table, int iva, int trimForYearConfig);
    void updateLock();
    void writeHtml(QString filename, QString html);
    QString createHtmlHeader();
    QString createHtmlTables(int trimForYearConfig);
    QString createHtmlTableIngresos(int trimForYearConfig);
    QString createHtmlTableGastos(int trimForYearConfig);


private:
    Ui::Contabilidad *ui;
};

#endif // CONTABILIDAD_H
