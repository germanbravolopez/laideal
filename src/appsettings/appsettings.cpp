#include "appsettings.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QJsonDocument>
#include <QDebug>

AppSettings *AppSettings::s_instance = nullptr;

AppSettings *AppSettings::instance()
{
    if (!s_instance)
        s_instance = new AppSettings();
    return s_instance;
}

AppSettings::AppSettings()
    : m_filePath(QDir::homePath() + "/.laideal_settings.json")
{
    applyDefaults();
}

void AppSettings::applyDefaults()
{
    // Sensible defaults so reports work even before the user configures anything.
    if (businessName().isEmpty())
        setStr({"business", "name"}, "Tintorería La Ideal");
    if (businessAddress().isEmpty())
        setStr({"business", "address"}, "Plaza San Pantaleón 1, bajo 2");
    if (businessCity().isEmpty())
        setStr({"business", "city"}, "18012 Granada");
    if (ivaRate() == 0.0)
        setDbl({"taxes", "iva_rate"}, 21.0);
    if (str({"verifactu", "environment"}).isEmpty())
        setStr({"verifactu", "environment"}, "TESTING");
    if (reportsRoot().isEmpty())
        setStr({"reports", "root"}, QDir::homePath() + "/Tintoreria");
}

bool AppSettings::load()
{
    QFile file(m_filePath);

    if (!file.exists()) {
        migrateFromLegacyFiles();
        applyDefaults();
        save();
        qDebug() << "AppSettings: created new settings file at" << m_filePath;
        return false;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "AppSettings: cannot open" << m_filePath;
        applyDefaults();
        return false;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    file.close();

    if (err.error != QJsonParseError::NoError) {
        qWarning() << "AppSettings: JSON parse error:" << err.errorString();
        applyDefaults();
        return false;
    }

    m_data = doc.object();
    migrateLegacyKeys();
    applyDefaults();
    qDebug() << "AppSettings: loaded from" << m_filePath;
    return true;
}

bool AppSettings::save() const
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "AppSettings: cannot write to" << m_filePath;
        return false;
    }
    file.write(QJsonDocument(m_data).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

void AppSettings::migrateLegacyKeys()
{
    // Fold legacy {reports.contabilidad_path, reports.listados_*_path} into
    // a single reports.root. Heuristic: strip the well-known suffix from any
    // of the three legacy values; fall back to the parent dir of contabilidad_path;
    // fall back to the parent's parent of listados_prendas_path. Idempotent -
    // does nothing once reports.root is set.
    if (reportsRoot().isEmpty()) {
        const QString legacyContab  = str({"reports", "contabilidad_path"});
        const QString legacyPrendas = str({"reports", "listados_prendas_path"});
        const QString legacyGastos  = str({"reports", "listados_gastos_path"});

        auto stripSuffix = [](QString p, const QString &suffix) -> QString {
            p = QDir::cleanPath(p);
            if (p.endsWith("/" + suffix, Qt::CaseInsensitive))
                return p.left(p.size() - suffix.size() - 1);
            return QString();
        };

        QString root;
        if (!legacyContab.isEmpty())
            root = stripSuffix(legacyContab, "Contabilidad");
        if (root.isEmpty() && !legacyPrendas.isEmpty())
            root = stripSuffix(legacyPrendas, "Listados/Prendas");
        if (root.isEmpty() && !legacyGastos.isEmpty())
            root = stripSuffix(legacyGastos, "Listados/Gastos");
        // Last-resort fallback: the parent dir of contabilidad_path, even if the
        // suffix doesn't match. Better than nothing - the user can still edit it.
        if (root.isEmpty() && !legacyContab.isEmpty())
            root = QFileInfo(QDir::cleanPath(legacyContab)).absolutePath();

        if (!root.isEmpty()) {
            setStr({"reports", "root"}, root);
            qDebug() << "AppSettings: migrated three legacy report paths into reports.root =" << root;
        }
    }

    // Drop obsolete keys (no longer read; clutter otherwise).
    removeNested(m_data, {"reports", "contabilidad_path"});
    removeNested(m_data, {"reports", "listados_prendas_path"});
    removeNested(m_data, {"reports", "listados_gastos_path"});
    removeNested(m_data, {"print",   "template_path"});
    removeNested(m_data, {"print",   "script_path"});
    removeNested(m_data, {"app",     "icon_path"});
}

void AppSettings::migrateFromLegacyFiles()
{
    // ~/.laideal_cfg  →  verifactu.nif / verifactu.company_name
    QFile cfgFile(QDir::homePath() + "/.laideal_cfg");
    if (cfgFile.exists() && cfgFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&cfgFile);
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (line.startsWith("nif="))
                setStr({"verifactu", "nif"}, line.mid(4).trimmed());
            else if (line.startsWith("name="))
                setStr({"verifactu", "company_name"}, line.mid(5).trimmed());
        }
        cfgFile.close();
        qDebug() << "AppSettings: migrated emitter config from ~/.laideal_cfg";
    }

    // ~/.verifactu_key  →  verifactu.service_key
    QFile keyFile(QDir::homePath() + "/.verifactu_key");
    if (keyFile.exists() && keyFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&keyFile);
        setStr({"verifactu", "service_key"}, in.readLine().trimmed());
        keyFile.close();
        qDebug() << "AppSettings: migrated service key from ~/.verifactu_key";
    }
}

// ---------------------------------------------------------------------------
// Database
// ---------------------------------------------------------------------------
QString AppSettings::dbPath() const { return str({"database", "path"}); }
void    AppSettings::setDbPath(const QString &v) { setStr({"database", "path"}, v); }

// ---------------------------------------------------------------------------
// Reports - single user-configurable root + hardcoded subdirs
// ---------------------------------------------------------------------------
QString AppSettings::reportsRoot() const          { return str({"reports", "root"}); }
void    AppSettings::setReportsRoot(const QString &v) { setStr({"reports", "root"}, v); }

QString AppSettings::contabilidadPath() const     { return QDir(reportsRoot()).filePath("Contabilidad"); }
QString AppSettings::listadosPrendasPath() const  { return QDir(reportsRoot()).filePath("Listados/Prendas"); }
QString AppSettings::listadosGastosPath() const   { return QDir(reportsRoot()).filePath("Listados/Gastos"); }

// ---------------------------------------------------------------------------
// Print
// ---------------------------------------------------------------------------
bool AppSettings::enablePrinting() const { return str({"print", "enable"}) == "Yes"; }
void AppSettings::setEnablePrinting(bool v) { setStr({"print", "enable"}, v ? "Yes" : "No"); }

QString AppSettings::ticketExcelPath()       { return QDir::homePath() + "/.laideal_ticket.xlsx"; }
QString AppSettings::ticketPrintScriptPath() { return QDir::homePath() + "/.laideal_print.vbs"; }

// ---------------------------------------------------------------------------
// Business
// ---------------------------------------------------------------------------
QString AppSettings::businessName() const    { return str({"business", "name"}); }
void    AppSettings::setBusinessName(const QString &v) { setStr({"business", "name"}, v); }

QString AppSettings::businessAddress() const { return str({"business", "address"}); }
void    AppSettings::setBusinessAddress(const QString &v) { setStr({"business", "address"}, v); }

QString AppSettings::businessCity() const    { return str({"business", "city"}); }
void    AppSettings::setBusinessCity(const QString &v) { setStr({"business", "city"}, v); }

QString AppSettings::businessPhone() const   { return str({"business", "phone"}); }
void    AppSettings::setBusinessPhone(const QString &v) { setStr({"business", "phone"}, v); }

// ---------------------------------------------------------------------------
// Taxes
// ---------------------------------------------------------------------------
double AppSettings::ivaRate() const    { return dbl({"taxes", "iva_rate"}, 21.0); }
void   AppSettings::setIvaRate(double v) { setDbl({"taxes", "iva_rate"}, v); }

// ---------------------------------------------------------------------------
// Verifactu
// ---------------------------------------------------------------------------
QString AppSettings::verifactuNif() const        { return str({"verifactu", "nif"}); }
void    AppSettings::setVerifactuNif(const QString &v) { setStr({"verifactu", "nif"}, v); }

QString AppSettings::verifactuName() const       { return str({"verifactu", "company_name"}); }
void    AppSettings::setVerifactuName(const QString &v) { setStr({"verifactu", "company_name"}, v); }

QString AppSettings::verifactuServiceKey() const { return str({"verifactu", "service_key"}); }
void    AppSettings::setVerifactuServiceKey(const QString &v) { setStr({"verifactu", "service_key"}, v); }

bool AppSettings::verifactuProduction() const { return str({"verifactu", "environment"}) == "PRODUCTION"; }
void AppSettings::setVerifactuProduction(bool v) { setStr({"verifactu", "environment"}, v ? "PRODUCTION" : "TESTING"); }

// ---------------------------------------------------------------------------
// JSON helpers
// ---------------------------------------------------------------------------
QJsonObject AppSettings::nested(const QJsonObject &root, const QStringList &path) const
{
    QJsonObject cur = root;
    for (int i = 0; i < path.size() - 1; ++i) {
        if (!cur.contains(path[i]) || !cur[path[i]].isObject())
            return {};
        cur = cur[path[i]].toObject();
    }
    return cur;
}

void AppSettings::setNested(QJsonObject &root, const QStringList &path, const QJsonValue &value)
{
    if (path.isEmpty()) return;
    if (path.size() == 1) { root[path[0]] = value; return; }

    QJsonObject child = root.contains(path[0]) && root[path[0]].isObject()
                        ? root[path[0]].toObject()
                        : QJsonObject{};
    setNested(child, path.mid(1), value);
    root[path[0]] = child;
}

void AppSettings::removeNested(QJsonObject &root, const QStringList &path)
{
    if (path.isEmpty()) return;
    if (path.size() == 1) { root.remove(path[0]); return; }
    if (!root.contains(path[0]) || !root[path[0]].isObject()) return;

    QJsonObject child = root[path[0]].toObject();
    removeNested(child, path.mid(1));
    root[path[0]] = child;
}

QString AppSettings::str(const QStringList &path, const QString &def) const
{
    QJsonObject parent = nested(m_data, path);
    if (parent.isEmpty()) return def;
    QJsonValue v = parent[path.last()];
    return v.isString() ? v.toString() : def;
}

double AppSettings::dbl(const QStringList &path, double def) const
{
    QJsonObject parent = nested(m_data, path);
    if (parent.isEmpty()) return def;
    QJsonValue v = parent[path.last()];
    return v.isDouble() ? v.toDouble() : def;
}

bool AppSettings::bln(const QStringList &path, bool def) const
{
    QJsonObject parent = nested(m_data, path);
    if (parent.isEmpty()) return def;
    QJsonValue v = parent[path.last()];
    return v.isBool() ? v.toBool() : def;
}

void AppSettings::setStr(const QStringList &path, const QString &value)
{
    setNested(m_data, path, QJsonValue(value));
}

void AppSettings::setDbl(const QStringList &path, double value)
{
    setNested(m_data, path, QJsonValue(value));
}

void AppSettings::setBln(const QStringList &path, bool value)
{
    setNested(m_data, path, QJsonValue(value));
}
