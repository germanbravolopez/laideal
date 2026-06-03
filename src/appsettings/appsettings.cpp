#include "appsettings.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QJsonDocument>
#include <QDebug>

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <dpapi.h>
#endif

namespace {
// Marker for a value encrypted at rest. A stored value without this prefix is
// treated as legacy plaintext (and re-encrypted on the next launch).
const QString kEncPrefix = QStringLiteral("dpapi:v1:");

bool isEncrypted(const QString &stored) { return stored.startsWith(kEncPrefix); }

#ifdef Q_OS_WIN
// Encrypt with Windows DPAPI scoped to the current user (CRYPTPROTECT_UI_FORBIDDEN
// = no UI). On failure, fall back to returning the plaintext rather than losing the
// secret - better a working-but-unencrypted key than a broken AEAT connection.
QString encryptSecret(const QString &plain)
{
    if (plain.isEmpty()) return plain;
    const QByteArray in = plain.toUtf8();
    DATA_BLOB inBlob{ static_cast<DWORD>(in.size()), reinterpret_cast<BYTE*>(const_cast<char*>(in.constData())) };
    DATA_BLOB outBlob{ 0, nullptr };
    if (!CryptProtectData(&inBlob, L"laideal verifactu service key", nullptr, nullptr,
                          nullptr, CRYPTPROTECT_UI_FORBIDDEN, &outBlob)) {
        qWarning() << "AppSettings: CryptProtectData failed; storing service key unencrypted";
        return plain;
    }
    const QByteArray cipher(reinterpret_cast<const char*>(outBlob.pbData), static_cast<int>(outBlob.cbData));
    LocalFree(outBlob.pbData);
    return kEncPrefix + QString::fromLatin1(cipher.toBase64());
}

// Decrypt a value produced by encryptSecret. Legacy plaintext (no prefix) is
// returned unchanged. A decrypt failure (e.g. the file was copied from another
// Windows user/machine) returns empty so the caller surfaces a clear "no key".
QString decryptSecret(const QString &stored)
{
    if (!isEncrypted(stored)) return stored;
    const QByteArray cipher = QByteArray::fromBase64(stored.mid(kEncPrefix.size()).toLatin1());
    if (cipher.isEmpty()) return QString();
    DATA_BLOB inBlob{ static_cast<DWORD>(cipher.size()), reinterpret_cast<BYTE*>(const_cast<char*>(cipher.constData())) };
    DATA_BLOB outBlob{ 0, nullptr };
    if (!CryptUnprotectData(&inBlob, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &outBlob)) {
        qWarning() << "AppSettings: CryptUnprotectData failed (key bound to a different Windows user/machine?); clearing field";
        return QString();
    }
    const QString plain = QString::fromUtf8(reinterpret_cast<const char*>(outBlob.pbData), static_cast<int>(outBlob.cbData));
    LocalFree(outBlob.pbData);
    return plain;
}
#else
// Non-Windows builds (none shipped) keep the value as-is so the code still compiles.
QString encryptSecret(const QString &plain) { return plain; }
QString decryptSecret(const QString &stored) { return isEncrypted(stored) ? stored.mid(kEncPrefix.size()) : stored; }
#endif
} // namespace

// Thin public delegators over the file-local secret helpers above, so the
// encryption logic is unit-testable (the internal getters/setters keep calling
// the unqualified helpers directly).
QString AppSettings::dpapiEncrypt(const QString &plain)   { return encryptSecret(plain); }
QString AppSettings::dpapiDecrypt(const QString &stored)  { return decryptSecret(stored); }
bool    AppSettings::dpapiIsEncrypted(const QString &stored) { return isEncrypted(stored); }

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
    if (!m_data.value("verifactu").toObject().contains("pending_recovery_enabled"))
        setBln({"verifactu", "pending_recovery_enabled"}, true);
    if (verifactuPendingRecoveryFloorDate().isEmpty())
        setStr({"verifactu", "pending_recovery_floor_date"}, "2026-09-01");
    if (reportsRoot().isEmpty())
        setStr({"reports", "root"}, QDir::homePath() + "/Tintoreria");
    // Default: opt-in startup check. bln() has a default-fallback parameter,
    // but the key must be persisted so future save() preserves it across upgrades.
    if (!m_data.value("updater").toObject().contains("check_on_startup"))
        setBln({"updater", "check_on_startup"}, true);
    // Verifactu Req. 4: backups live next to dbPath() (BackupManager derives
    // the directory). Default opt-in.
    if (!m_data.value("backup").toObject().contains("enabled"))
        setBln({"backup", "enabled"}, true);
    // Thermal printer roll width (mm). Default 80; persisted so an upgrade keeps it.
    if (!m_data.value("print").toObject().contains("paper_width_mm"))
        setDbl({"print", "paper_width_mm"}, 80);
}

bool AppSettings::load()
{
    QFile file(m_filePath);

    if (!file.exists()) {
        migrateFromLegacyFiles();
        encryptSecretsAtRest();   // a key brought in from ~/.verifactu_key must not land as plaintext
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
    const bool reEncrypted = encryptSecretsAtRest();
    applyDefaults();
    // Persist immediately if a legacy plaintext key was just encrypted, so the
    // plaintext no longer lingers on disk until the next unrelated settings change.
    if (reEncrypted)
        save();
    qDebug() << "AppSettings: loaded from" << m_filePath;
    return true;
}

bool AppSettings::save() const
{
    // Atomic write: serialize to a sibling .tmp file, fsync-equivalent flush, then
    // rename over the target. A crash mid-write leaves the previous settings intact
    // instead of producing a zero-byte JSON that would lose the DB path / Verifactu
    // credentials on next start.
    const QString tmpPath = m_filePath + ".tmp";
    QFile tmp(tmpPath);
    if (!tmp.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "AppSettings: cannot write to" << tmpPath;
        return false;
    }
    const QByteArray bytes = QJsonDocument(m_data).toJson(QJsonDocument::Indented);
    if (tmp.write(bytes) != bytes.size()) {
        qWarning() << "AppSettings: short write to" << tmpPath;
        tmp.close();
        QFile::remove(tmpPath);
        return false;
    }
    tmp.flush();
    tmp.close();

    // QFile::rename is not atomic on Windows when the target exists, so remove first.
    // The window between remove and rename is small; the .tmp still exists on disk
    // so a crash here is recoverable by hand.
    QFile::remove(m_filePath);
    if (!QFile::rename(tmpPath, m_filePath)) {
        qWarning() << "AppSettings: rename" << tmpPath << "->" << m_filePath << "failed";
        return false;
    }
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

bool AppSettings::encryptSecretsAtRest()
{
    const QString stored = str({"verifactu", "service_key"});
    if (stored.isEmpty() || isEncrypted(stored))
        return false;
    setStr({"verifactu", "service_key"}, encryptSecret(stored));
    qDebug() << "AppSettings: encrypted plaintext Verifactu service key at rest (DPAPI)";
    return true;
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

QString AppSettings::printerName() const          { return str({"print", "printer_name"}); }
void    AppSettings::setPrinterName(const QString &v) { setStr({"print", "printer_name"}, v); }

int  AppSettings::paperWidthMm() const  { return static_cast<int>(dbl({"print", "paper_width_mm"}, 80)); }
void AppSettings::setPaperWidthMm(int v) { setDbl({"print", "paper_width_mm"}, v); }

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

// Stored encrypted at rest (Windows DPAPI, per-user); callers always see plaintext.
QString AppSettings::verifactuServiceKey() const { return decryptSecret(str({"verifactu", "service_key"})); }
void    AppSettings::setVerifactuServiceKey(const QString &v) { setStr({"verifactu", "service_key"}, encryptSecret(v)); }

bool AppSettings::verifactuProduction() const { return str({"verifactu", "environment"}) == "PRODUCTION"; }
void AppSettings::setVerifactuProduction(bool v) { setStr({"verifactu", "environment"}, v ? "PRODUCTION" : "TESTING"); }

bool    AppSettings::verifactuPendingRecoveryEnabled() const   { return bln({"verifactu", "pending_recovery_enabled"}, true); }
void    AppSettings::setVerifactuPendingRecoveryEnabled(bool v) { setBln({"verifactu", "pending_recovery_enabled"}, v); }
QString AppSettings::verifactuPendingRecoveryFloorDate() const { return str({"verifactu", "pending_recovery_floor_date"}); }
void    AppSettings::setVerifactuPendingRecoveryFloorDate(const QString &v) { setStr({"verifactu", "pending_recovery_floor_date"}, v); }

// ---------------------------------------------------------------------------
// Updater
// ---------------------------------------------------------------------------
bool AppSettings::checkUpdatesOnStartup() const    { return bln({"updater", "check_on_startup"}, true); }
void AppSettings::setCheckUpdatesOnStartup(bool v) { setBln({"updater", "check_on_startup"}, v); }

// ---------------------------------------------------------------------------
// Backup (Verifactu Req. 4)
// ---------------------------------------------------------------------------
bool    AppSettings::backupEnabled() const            { return bln({"backup", "enabled"}, true); }
void    AppSettings::setBackupEnabled(bool v)         { setBln({"backup", "enabled"}, v); }
QString AppSettings::backupLastTime() const           { return str({"backup", "last_time"}); }
void    AppSettings::setBackupLastTime(const QString &v) { setStr({"backup", "last_time"}, v); }

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
