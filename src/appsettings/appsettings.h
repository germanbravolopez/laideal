#ifndef APPSETTINGS_H
#define APPSETTINGS_H

#include <QString>
#include <QJsonObject>
#include <QStringList>

// Singleton that loads/saves ~/.laideal_settings.json and exposes all
// user-configurable settings. Call load() once in main() before MainWindow
// is constructed. All modules read from instance() at point of use.
class AppSettings
{
public:
    static AppSettings *instance();

    // Load from ~/.laideal_settings.json. Migrates legacy ~/.laideal_cfg and
    // ~/.verifactu_key on first run. Returns true if the file was found and parsed.
    bool load();

    // Persist current state back to the JSON file.
    bool save() const;

    // Injection seam: load from an explicit file path instead of the fixed
    // ~/.laideal_settings.json, resetting in-memory state first so repeated calls
    // don't accumulate. Runs the full parse + legacy-key migration + encrypt-at-rest
    // + defaults pipeline against that file (and rewrites it if a plaintext secret
    // was encrypted), so those getters are testable without touching the real
    // settings file. Returns what load() returns. Not used in production.
    bool loadFrom(const QString &path);

    QString filePath() const { return m_filePath; }

    // --- Language ("es" / "en") ---
    // Selects the Qt translation loaded at startup (standard dialogs, Yes/No, etc.).
    // Default "es"; a change takes effect on the next launch.
    QString language() const;
    void setLanguage(const QString &v);

    // --- Database ---
    QString dbPath() const;
    void setDbPath(const QString &v);

    // --- Report output directory ---
    // Single user-configurable root; subdirs are hardcoded inside the getters
    // so the JSON exposes only one path. mkpath() is the caller's responsibility
    // (run on demand, not on settings save) - see Contabilidad / Listado.
    QString reportsRoot() const;
    void setReportsRoot(const QString &v);
    QString contabilidadPath() const;        // <root>/Contabilidad
    QString listadosPrendasPath() const;     // <root>/Listados/Prendas
    QString listadosGastosPath() const;      // <root>/Listados/Gastos

    // --- Print (direct ESC/POS to the thermal printer) ---
    bool enablePrinting() const;
    void setEnablePrinting(bool v);
    // Windows printer-queue name the ESC/POS bytes are sent to (RAW). Empty =
    // use the system default printer.
    QString printerName() const;
    void    setPrinterName(const QString &v);
    // Roll width in mm (58 or 80). Drives the receipt's column layout: 80 -> 576
    // printable dots, 58 -> 420. Defaults to 80.
    int  paperWidthMm() const;
    void setPaperWidthMm(int v);
    // When on, route the ESC/POS bytes through Epson's Status API (EPSStmApi.dll)
    // instead of the RAW spooler, to read back paper-out / cover-open / cutter
    // status after printing. Off by default; falls back to RAW if unavailable.
    bool useStatusApi() const;
    void setUseStatusApi(bool v);

    // --- Business identity ---
    QString businessName() const;
    void setBusinessName(const QString &v);
    QString businessAddress() const;
    void setBusinessAddress(const QString &v);
    QString businessCity() const;
    void setBusinessCity(const QString &v);
    QString businessPhone() const;
    void setBusinessPhone(const QString &v);

    // --- Taxes ---
    double ivaRate() const;      // percentage, e.g. 21.0
    void setIvaRate(double v);

    // --- Verifactu ---
    QString verifactuNif() const;
    void setVerifactuNif(const QString &v);
    QString verifactuName() const;
    void setVerifactuName(const QString &v);
    QString verifactuServiceKey() const;
    void setVerifactuServiceKey(const QString &v);

    // Secret-at-rest helpers (Windows DPAPI, per-user). The service-key getter/
    // setter use these internally; exposed as statics so the encrypt->decrypt
    // round-trip, the `dpapi:v1:` marker and the legacy-plaintext passthrough are
    // unit-testable without the settings file. On non-Windows builds they are a
    // passthrough. `dpapiEncrypt("")` returns "".
    static QString dpapiEncrypt(const QString &plain);
    static QString dpapiDecrypt(const QString &stored);
    static bool    dpapiIsEncrypted(const QString &stored);
    bool verifactuProduction() const;
    void setVerifactuProduction(bool v);
    // Startup recovery for verifactu_estado=PENDIENTE rows. Disabled-by-default
    // behaviour would silently leave durability gaps unresolved; default is on
    // but gated by a floor date so legacy pre-Verifactu tickets do not surface.
    bool    verifactuPendingRecoveryEnabled() const;
    void    setVerifactuPendingRecoveryEnabled(bool v);
    // ISO yyyy-MM-dd. Rows with fecha_recepcion strictly before this date are
    // ignored by the startup recovery dialog. Defaults to the planned PROD
    // cutover (2026-09-01) so TESTING-era tickets do not get flagged.
    QString verifactuPendingRecoveryFloorDate() const;
    void    setVerifactuPendingRecoveryFloorDate(const QString &v);

    // --- Updater ---
    bool checkUpdatesOnStartup() const;
    void setCheckUpdatesOnStartup(bool v);

    // --- Backup (Verifactu Req. 4: archive period, RD 1007/2023 Art. 8.2.c) ---
    // Backups are written next to dbPath() (same directory as the live DB);
    // there is no separate "backup root" setting.
    bool    backupEnabled() const;
    void    setBackupEnabled(bool v);
    // ISO-8601 timestamp of the last successful backup (empty = never).
    QString backupLastTime() const;
    void    setBackupLastTime(const QString &v);

private:
    AppSettings();
    void migrateFromLegacyFiles();
    // Folds legacy {reports.contabilidad_path, reports.listados_*_path,
    // print.template_path, print.script_path} into a single reports.root and
    // removes the obsolete keys. Idempotent.
    void migrateLegacyKeys();
    void applyDefaults();
    // Encrypts any at-rest secret (currently the Verifactu service key) that is
    // still stored in plaintext, using Windows DPAPI scoped to the current user.
    // Idempotent. Returns true if it changed m_data (so the caller can persist).
    bool encryptSecretsAtRest();

    QString str(const QStringList &path, const QString &def = {}) const;
    double  dbl(const QStringList &path, double def = 0.0) const;
    bool    bln(const QStringList &path, bool def = false) const;
    void    setStr(const QStringList &path, const QString &value);
    void    setDbl(const QStringList &path, double value);
    void    setBln(const QStringList &path, bool value);

    // Navigate / create nested JSON objects along a key path.
    QJsonObject nested(const QJsonObject &root, const QStringList &path) const;
    void        setNested(QJsonObject &root, const QStringList &path, const QJsonValue &value);
    void        removeNested(QJsonObject &root, const QStringList &path);

    QJsonObject m_data;
    QString     m_filePath;

    static AppSettings *s_instance;
};

#endif // APPSETTINGS_H
