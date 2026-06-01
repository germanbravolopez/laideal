#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QCheckBox>

// Settings dialog - all user-configurable options across the application.
// Reads from and writes to AppSettings. Call exec(); on Accepted the caller
// should re-apply settings that take effect at runtime (Verifactu).
// DB path changes require an application restart.
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

public:
    void browsePath(QLineEdit *target, bool directory);

signals:
    // Emitted when the user clicks "Probar conexión" in the Verifactu tab.
    // The connection test is performed by MainWindow (which owns the VerifactuIntegration)
    // so this module does not depend on verifactu.
    void testConnectionRequested(const QString &nif,
                                 const QString &name,
                                 const QString &serviceKey,
                                 bool production);

private slots:
    void accept() override;

private:
    void buildGeneralTab(class QTabWidget *tabs);
    void buildReportsTab(class QTabWidget *tabs);
    void buildBusinessTab(class QTabWidget *tabs);
    void buildVerifactuTab(class QTabWidget *tabs);

    QLineEdit *m_dbPath;
    QLineEdit *m_ivaRate;
    QCheckBox *m_enablePrinting;
    QCheckBox *m_checkUpdatesOnStartup;

    QLineEdit *m_reportsRoot;

    QLineEdit *m_businessName;
    QLineEdit *m_businessAddress;
    QLineEdit *m_businessCity;
    QLineEdit *m_businessPhone;

    QLineEdit *m_vNif;
    QLineEdit *m_vName;
    QLineEdit *m_vKey;
    QCheckBox *m_vProduction;
    QCheckBox *m_vPendingRecoveryEnabled;
    class QDateEdit *m_vPendingRecoveryFloor;
};

#endif // SETTINGSDIALOG_H
