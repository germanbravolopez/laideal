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
    // The owner's certificates in the Windows store, as (label, thumbprint), for the
    // direct AEAT connection. Given by MainWindow: this module does not read the store.
    void setCertificateChoices(const QList<QPair<QString, QString>> &choices);

signals:
    // Emitted when the user clicks "Probar conexión" in the Verifactu tab.
    // The connection test is performed by MainWindow (which owns the VerifactuIntegration)
    // so this module does not depend on verifactu.
    void testConnectionRequested(const QString &nif,
                                 const QString &name,
                                 const QString &serviceKey,
                                 bool production);
    // "Prueba con la AEAT": the direct connection's self-test with the form's values.
    void aeatSelfTestRequested(const QString &nif, const QString &name, const QString &thumbprint,
                               const QString &certificateFile, const QString &certificatePassword);

private slots:
    void accept() override;

private:
    void buildGeneralTab(class QTabWidget *tabs);
    void buildReportsTab(class QTabWidget *tabs);
    void buildBusinessTab(class QTabWidget *tabs);
    void buildVerifactuTab(class QTabWidget *tabs);

    class QComboBox *m_language;      // "es" / "en" (Qt standard-dialog language)
    QLineEdit *m_dbPath;
    QCheckBox *m_enablePrinting;
    class QComboBox *m_printerName;   // editable: queue name, blank = default printer
    class QComboBox *m_paperWidth;    // 58 / 80 mm
    QCheckBox *m_useStatusApi;        // route via Epson Status API + read device status
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
    class QComboBox *m_vConnection;       // gateway / direct AEAT
    class QComboBox *m_vCertificate;      // a store thumbprint, or "" for a .pfx file
    QLineEdit *m_vCertFile;
    QLineEdit *m_vCertPassword;
    QWidget   *m_vCertFileRow;
    QWidget   *m_vDirectGroup;

    QString selectedThumbprint() const;
    void updateDirectRows();
};

#endif // SETTINGSDIALOG_H
