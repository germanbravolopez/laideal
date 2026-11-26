#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QCheckBox>
#include <QPair>
#include <functional>

// Settings dialog - all user-configurable options across the application.
// Reads from and writes to AppSettings. Call exec(); on Accepted the caller
// should re-apply settings that take effect at runtime (Verifactu).
// DB path changes require an application restart.
class QLabel;
class QFormLayout;
class AppSettings;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    void browsePath(QLineEdit *target, bool directory);
    // The owner's certificates in the Windows store, as (label, thumbprint), for the
    // direct AEAT connection. Given by MainWindow: this module does not read the store.
    void setCertificateChoices(const QList<QPair<QString, QString>> &choices);
    // Records the direct connection generated and AEAT has not confirmed: while there
    // are any, the connection cannot be switched back to the gateway.
    void setDirectPendingCount(int count) { m_directPending = count; }
    // Invoices the real AEAT refused (the red count of the status bar): production is not left behind them.
    void setProductionRefusedCount(int count) { m_productionRefused = count; }
    // Run on accept when the direct connection goes to PRODUCTION, before the question:
    // the reason it cannot (certificate, no answer from the real AEAT), or empty. Given by
    // MainWindow (this module does not know AEAT); unset: nothing is checked.
    using ProductionCheck = std::function<QString(const QString &nif, const QString &name, const QString &thumbprint,
                                                  const QString &certificateFile, const QString &certificatePassword)>;
    void setProductionCheck(ProductionCheck check) { m_productionCheck = std::move(check); }
    // Asked on accept when the database path is not an existing file; true keeps it.
    // Defaults to a Yes/No question; tests and scripted runs replace it.
    using MissingDbConfirm = std::function<bool(const QString &path)>;
    static void setMissingDbConfirm(MissingDbConfirm confirm);

signals:
    // Emitted when the user clicks "Probar conexión" in the Verifactu tab.
    // The connection test is performed by MainWindow (which owns the VerifactuIntegration)
    // so this module does not depend on verifactu.
    void testConnectionRequested(const QString &nif,
                                 const QString &name,
                                 const QString &serviceKey,
                                 bool production);
    // "Ensayo en el entorno de pruebas de la AEAT": the direct connection's self-test with the form's values.
    void aeatSelfTestRequested(const QString &nif, const QString &name, const QString &thumbprint,
                               const QString &certificateFile, const QString &certificatePassword);
    // "Probar conexión" of the direct connection: certificate and one query, nothing is sent.
    void aeatConnectionTestRequested(const QString &nif, const QString &name, const QString &thumbprint,
                                     const QString &certificateFile, const QString &certificatePassword,
                                     bool production);

private slots:
    void accept() override;

private:
    void buildGeneralTab(class QTabWidget *tabs);
    void buildReportsTab(class QTabWidget *tabs);
    void buildBusinessTab(class QTabWidget *tabs);
    void buildVerifactuTab(class QTabWidget *tabs);
    QFormLayout *buildGatewayGroup(const AppSettings *s);
    QFormLayout *buildDirectGroup(const AppSettings *s);
    void addVerifactuCommonRows(QFormLayout *form, const AppSettings *s);

    bool directSelected() const;
    bool confirmConnectionChange(const AppSettings *s);
    bool confirmDatabasePath();
    bool storeSettings(AppSettings *s);

    class QComboBox *m_language = nullptr;      // "es" / "en" (Qt standard-dialog language)
    QLineEdit *m_dbPath = nullptr;
    QLineEdit *m_backupSecondFolder = nullptr;
    QCheckBox *m_enablePrinting = nullptr;
    class QComboBox *m_printerName = nullptr;   // editable: queue name, blank = default printer
    class QComboBox *m_paperWidth = nullptr;    // 58 / 80 mm
    QCheckBox *m_useStatusApi = nullptr;        // route via Epson Status API + read device status
    QCheckBox *m_checkUpdatesOnStartup = nullptr;

    QLineEdit *m_reportsRoot = nullptr;

    QLineEdit *m_businessName = nullptr;
    QLineEdit *m_businessAddress = nullptr;
    QLineEdit *m_businessCity = nullptr;
    QLineEdit *m_businessPhone = nullptr;

    QLineEdit *m_vNif = nullptr;
    QLineEdit *m_vName = nullptr;
    QLineEdit *m_vKey = nullptr;
    QCheckBox *m_vProduction = nullptr;
    QCheckBox *m_vPendingRecoveryEnabled = nullptr;
    class QDateEdit *m_vPendingRecoveryFloor = nullptr;
    class QComboBox *m_vConnection = nullptr;       // gateway / direct AEAT
    class QComboBox *m_vCertificate = nullptr;      // a store thumbprint, or "" for a .pfx file
    QLineEdit *m_vCertFile = nullptr;
    QLineEdit *m_vCertPassword = nullptr;
    QWidget   *m_vCertFileRow = nullptr;
    QWidget   *m_vGatewayGroup = nullptr;         // the gateway's rows: shown only while it is the connection
    QWidget   *m_vDirectGroup = nullptr;
    QWidget   *m_vSelfTest = nullptr;   // the self-test button: hidden while the shop works in pre-production
    bool       m_selfTestAllowed = true;
    QCheckBox *m_vDirectProduction = nullptr;     // direct connection: production AEAT (off: pre-production)
    int        m_directPending = 0;
    int        m_productionRefused = 0;
    ProductionCheck m_productionCheck;
    QLabel    *m_vKeyUnreadable = nullptr;
    QLabel    *m_vCertPasswordUnreadable = nullptr;
    QLabel    *m_vFloorFuture = nullptr;
    void updateFloorWarning();

    QString selectedThumbprint() const;
    void updateDirectRows();
};

#endif // SETTINGSDIALOG_H
