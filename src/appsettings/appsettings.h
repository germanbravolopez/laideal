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

    QString filePath() const { return m_filePath; }

    // --- Database ---
    QString dbPath() const;
    void setDbPath(const QString &v);

    // --- Application ---
    QString iconPath() const;
    void setIconPath(const QString &v);

    // --- Report output directories ---
    QString contabilidadPath() const;
    void setContabilidadPath(const QString &v);
    QString listadosPrendasPath() const;
    void setListadosPrendasPath(const QString &v);
    QString listadosGastosPath() const;
    void setListadosGastosPath(const QString &v);

    // --- Print resources ---
    QString printTemplatePath() const;
    void setPrintTemplatePath(const QString &v);
    QString printScriptPath() const;
    void setPrintScriptPath(const QString &v);

    // --- Business identity ---
    QString businessName() const;
    void setBusinessName(const QString &v);
    QString businessAddress() const;
    void setBusinessAddress(const QString &v);
    QString businessCity() const;
    void setBusinessCity(const QString &v);

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
    bool verifactuProduction() const;
    void setVerifactuProduction(bool v);

private:
    AppSettings();
    void migrateFromLegacyFiles();
    void applyDefaults();

    QString str(const QStringList &path, const QString &def = {}) const;
    double  dbl(const QStringList &path, double def = 0.0) const;
    bool    bln(const QStringList &path, bool def = false) const;
    void    setStr(const QStringList &path, const QString &value);
    void    setDbl(const QStringList &path, double value);
    void    setBln(const QStringList &path, bool value);

    // Navigate / create nested JSON objects along a key path.
    QJsonObject nested(const QJsonObject &root, const QStringList &path) const;
    void        setNested(QJsonObject &root, const QStringList &path, const QJsonValue &value);

    QJsonObject m_data;
    QString     m_filePath;

    static AppSettings *s_instance;
};

#endif // APPSETTINGS_H
