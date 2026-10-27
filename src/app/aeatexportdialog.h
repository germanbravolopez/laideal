#ifndef AEATEXPORTDIALOG_H
#define AEATEXPORTDIALOG_H

#include <QDialog>
#include <QSqlDatabase>
#include <functional>

class QDateEdit;
class QLineEdit;
namespace UiKit { class ResultPanel; }

// Facturas -> Exportar registros AEAT (XML): the invoices AEAT holds for a
// period, in one file for Hacienda (Art. 14.1 RD 1007/2023). Records from
// sql_lite::aeatExportRecords, written by writeAeatExportXml. Built in code with
// the shared UiKit style; the result (count, file link, warnings) is shown in it.
class AeatExportDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AeatExportDialog(const QSqlDatabase &database, QWidget *parent = nullptr);

    // Asked before an existing file is replaced; true to overwrite. Tests inject their answer.
    std::function<bool(const QString &file)> confirmOverwrite;

private slots:
    void onExportClicked();
    void onChooseFileClicked();
    void suggestFileName();

private:
    QSqlDatabase db;
    QDateEdit *m_deFrom = nullptr;
    QDateEdit *m_deTo = nullptr;
    QLineEdit *m_leFile = nullptr;
    bool m_fileChosen = false;   // typed or picked: no longer follows the dates
    UiKit::ResultPanel *m_lblResult = nullptr;
};

#endif // AEATEXPORTDIALOG_H
