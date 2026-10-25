#ifndef SELFCHECKDIALOG_H
#define SELFCHECKDIALOG_H

// The window of `laideal.exe --self-check` (see selfcheck.h): copies the newest backup
// and the settings to a temporary folder, runs the automatic checks, asks the guided
// ones with Sí / No and a note, writes the report to the Desktop and deletes the copies.

#include <QDialog>
#include <QDir>
#include <QSqlDatabase>
#include <QTemporaryDir>
#include <QVector>
#include <functional>

#include "selfcheck.h"

class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
namespace UiKit { class ResultPanel; }

class SelfCheckDialog : public QDialog
{
    Q_OBJECT

public:
    // `liveSettingsFile` is only read; AppSettings must hold it loaded.
    explicit SelfCheckDialog(const QString &liveSettingsFile, QWidget *parent = nullptr);
    ~SelfCheckDialog() override;

    // Where the report is written (the Desktop by default).
    void setReportFolder(const QString &folder) { m_reportFolder = folder; }
    QString reportFile() const { return m_reportFile; }
    const QVector<SelfCheck::Check> &checks() const { return m_checks; }

public slots:
    void start();

private slots:
    void answer(bool yes);

private:
    struct Question {
        int check;                         // index in m_checks
        QString text, info;
        QString openFile;                  // a PDF the user may open to compare
    };

    void buildUi();
    bool prepareCopy(QString *error);
    void runAutomatic();
    void printSample();
    void askNext();
    void finish();
    void cleanUp();
    int addCheck(const QString &id, const QString &title);
    void showCheck(int index);

    QString m_liveSettingsFile;
    QString m_liveDb;
    QString m_reportFolder;
    QString m_reportFile;
    QTemporaryDir m_work{ QDir::tempPath() + QStringLiteral("/laideal_comprobacion_XXXXXX") };
    QSqlDatabase m_db;
    SelfCheck::DataFacts m_facts;
    QVector<SelfCheck::Check> m_checks;
    QVector<Question> m_questions;
    int m_current = -1;

    QTableWidget *m_table = nullptr;
    QGroupBox *m_grpQuestion = nullptr;
    QLabel *m_lblQuestion = nullptr, *m_lblInfo = nullptr;
    QLineEdit *m_leNote = nullptr;
    QPushButton *m_btnYes = nullptr, *m_btnNo = nullptr, *m_btnOpen = nullptr;
    UiKit::ResultPanel *m_result = nullptr;
};

#endif // SELFCHECKDIALOG_H
