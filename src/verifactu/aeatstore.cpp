#include "aeatstore.h"

#include "aeathash.h"

#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

const QString AeatStore::kPending  = QStringLiteral("PENDIENTE");
const QString AeatStore::kAccepted = QStringLiteral("ACEPTADO");
const QString AeatStore::kRejected = QStringLiteral("RECHAZADO");
const QString AeatStore::kDuplicate = QStringLiteral("DUPLICADO");
const QString AeatStore::kCancelledAtAeat = QStringLiteral("ANULADA_EN_AEAT");

namespace {

const QString kDateFormat = QStringLiteral("dd-MM-yyyy");
const char *const kRecordColumns =
    "id, kind, issuer_nif, invoice_number, issue_date, total, hash, xml, generated_at, "
    "state, csv, error_code, error_description, attempts";

// A null QString binds as SQL NULL, which the NOT NULL text columns refuse.
QString text(const QString &value)
{
    return value.isNull() ? QStringLiteral("") : value;
}

QString kindText(AeatStore::Kind kind)
{
    return kind == AeatStore::Kind::Registration ? QStringLiteral("alta") : QStringLiteral("anulacion");
}

} // namespace

AeatStore::AeatStore(const QSqlDatabase &db, const QString &environment)
    : m_db(db), m_environment(environment)
{
}

bool AeatStore::open(bool *wasOpen)
{
    *wasOpen = m_db.isOpen();
    if (!*wasOpen && !m_db.open()) {
        m_lastError = m_db.lastError().text();
        qWarning() << "AeatStore: cannot open the database -" << m_lastError;
        return false;
    }
    return true;
}

void AeatStore::close(bool wasOpen)
{
    if (!wasOpen)
        m_db.close();
}

bool AeatStore::ensureSchema()
{
    bool wasOpen = false;
    if (!open(&wasOpen))
        return false;
    QSqlQuery q(m_db);
    const bool ok =
        q.exec("CREATE TABLE IF NOT EXISTS aeat_chain ("
               " environment TEXT NOT NULL, issuer_nif TEXT NOT NULL, invoice_number TEXT NOT NULL,"
               " issue_date TEXT NOT NULL, hash TEXT NOT NULL, generated_at TEXT NOT NULL DEFAULT '',"
               " PRIMARY KEY (environment, issuer_nif))")
        && q.exec("CREATE TABLE IF NOT EXISTS aeat_records ("
                  " id INTEGER PRIMARY KEY AUTOINCREMENT, environment TEXT NOT NULL, kind TEXT NOT NULL,"
                  " issuer_nif TEXT NOT NULL, invoice_number TEXT NOT NULL, issue_date TEXT NOT NULL,"
                  " total REAL NOT NULL DEFAULT 0, hash TEXT NOT NULL, xml TEXT NOT NULL, generated_at TEXT NOT NULL,"
                  " state TEXT NOT NULL, csv TEXT NOT NULL DEFAULT '', error_code TEXT NOT NULL DEFAULT '',"
                  " error_description TEXT NOT NULL DEFAULT '', attempts INTEGER NOT NULL DEFAULT 0,"
                  " last_attempt TEXT NOT NULL DEFAULT '')")
        && q.exec("CREATE INDEX IF NOT EXISTS aeat_records_invoice ON aeat_records (environment, invoice_number, kind)")
        && q.exec("CREATE TABLE IF NOT EXISTS aeat_chain_sync ("
                  " environment TEXT NOT NULL, issuer_nif TEXT NOT NULL, synced_at TEXT NOT NULL,"
                  " PRIMARY KEY (environment, issuer_nif))");
    if (!ok) {
        m_lastError = q.lastError().text();
        qWarning() << "AeatStore::ensureSchema failed -" << m_lastError;
    } else {
        // aeat_chain made by an earlier build of the research branch lacks it (fails once added).
        QSqlQuery(m_db).exec("ALTER TABLE aeat_chain ADD COLUMN generated_at TEXT NOT NULL DEFAULT ''");
    }
    close(wasOpen);
    return ok;
}

AeatRecord::PreviousRecord AeatStore::chainHead(const QString &issuerNif, bool *ok)
{
    AeatRecord::PreviousRecord head;
    if (ok)
        *ok = false;
    bool wasOpen = false;
    if (!open(&wasOpen))
        return head;
    QSqlQuery q(m_db);
    q.prepare("SELECT invoice_number, issue_date, hash, generated_at FROM aeat_chain WHERE environment = :e AND issuer_nif = :n");
    q.bindValue(":e", m_environment);
    q.bindValue(":n", issuerNif);
    const bool read = q.exec();
    if (read && q.next()) {
        head.invoice = { issuerNif, q.value(0).toString(), QDate::fromString(q.value(1).toString(), kDateFormat) };
        head.hash = q.value(2).toString();
        head.generatedAt = q.value(3).toString();
    }
    if (!read) {
        m_lastError = q.lastError().text();
        qWarning() << "AeatStore::chainHead: cannot read the chain -" << m_lastError;
    }
    if (ok)
        *ok = read;
    q.finish();
    close(wasOpen);
    return head;
}

bool AeatStore::setChainHead(const QString &issuerNif, const AeatRecord::PreviousRecord &head)
{
    bool wasOpen = false;
    if (!open(&wasOpen))
        return false;
    QSqlQuery q(m_db);
    if (head.isFirst()) {
        q.prepare("DELETE FROM aeat_chain WHERE environment = :e AND issuer_nif = :n");
    } else {
        q.prepare("INSERT OR REPLACE INTO aeat_chain (environment, issuer_nif, invoice_number, issue_date, hash, generated_at) "
                  "VALUES (:e, :n, :num, :d, :h, :g)");
        q.bindValue(":num", head.invoice.invoiceNumber);
        q.bindValue(":d", head.invoice.issueDate.toString(kDateFormat));
        q.bindValue(":h", head.hash);
        q.bindValue(":g", text(head.generatedAt));
    }
    q.bindValue(":e", m_environment);
    q.bindValue(":n", issuerNif);
    const bool ok = q.exec();
    if (ok)
        qDebug() << "AeatStore: chain of" << issuerNif << "(" << m_environment << ") now continues after"
                 << head.invoice.invoiceNumber << head.hash;
    else
        qWarning() << "AeatStore::setChainHead failed -" << q.lastError().text();
    close(wasOpen);
    return ok;
}

bool AeatStore::chainSynced(const QString &issuerNif)
{
    bool wasOpen = false;
    if (!open(&wasOpen))
        return false;
    QSqlQuery q(m_db);
    q.prepare("SELECT 1 FROM aeat_chain_sync WHERE environment = :e AND issuer_nif = :n");
    q.bindValue(":e", m_environment);
    q.bindValue(":n", issuerNif);
    const bool synced = q.exec() && q.next();
    q.finish();
    close(wasOpen);
    return synced;
}

bool AeatStore::markChainSynced(const QString &issuerNif)
{
    bool wasOpen = false;
    if (!open(&wasOpen))
        return false;
    QSqlQuery q(m_db);
    q.prepare("INSERT OR REPLACE INTO aeat_chain_sync (environment, issuer_nif, synced_at) VALUES (:e, :n, :t)");
    q.bindValue(":e", m_environment);
    q.bindValue(":n", issuerNif);
    q.bindValue(":t", QDateTime::currentDateTime().toString(Qt::ISODate));
    const bool ok = q.exec();
    close(wasOpen);
    return ok;
}

QStringList AeatStore::recordXmls()
{
    QStringList out;
    bool wasOpen = false;
    if (!open(&wasOpen))
        return out;
    QSqlQuery q(m_db);
    q.prepare("SELECT xml FROM aeat_records WHERE environment = :e ORDER BY id");
    q.bindValue(":e", m_environment);
    if (q.exec())
        while (q.next())
            out << q.value(0).toString();
    q.finish();
    close(wasOpen);
    return out;
}

bool AeatStore::seedChainHead(const QString &issuerNif, const AeatRecord::PreviousRecord &head)
{
    if (head.isFirst())
        return false;
    bool wasOpen = false;
    if (!open(&wasOpen))
        return false;
    QSqlQuery q(m_db);
    q.prepare("INSERT OR IGNORE INTO aeat_chain (environment, issuer_nif, invoice_number, issue_date, hash, generated_at) "
              "VALUES (:e, :n, :num, :d, :h, :g)");
    q.bindValue(":g", text(head.generatedAt));
    q.bindValue(":e", m_environment);
    q.bindValue(":n", issuerNif);
    q.bindValue(":num", head.invoice.invoiceNumber);
    q.bindValue(":d", head.invoice.issueDate.toString(kDateFormat));
    q.bindValue(":h", head.hash);
    const bool seeded = q.exec() && q.numRowsAffected() == 1;
    if (seeded)
        qDebug() << "AeatStore: chain of" << issuerNif << "(" << m_environment << ") starts after"
                 << head.invoice.invoiceNumber << head.hash;
    close(wasOpen);
    return seeded;
}

AeatStore::Record AeatStore::append(Kind kind, const QString &issuerNif,
                                    const std::function<Built(const AeatRecord::PreviousRecord &)> &build)
{
    Record stored;
    bool wasOpen = false;
    if (!open(&wasOpen))
        return stored;
    // One transaction: two records can never chain to the same previous one.
    if (!m_db.transaction()) {
        m_lastError = m_db.lastError().text();
        qWarning() << "AeatStore::append: no transaction -" << m_lastError;
        close(wasOpen);
        return stored;
    }
    bool headRead = false;
    const AeatRecord::PreviousRecord head = chainHead(issuerNif, &headRead);
    // An unreadable chain must never become "the first record".
    const Built built = headRead ? build(head) : Built();
    QSqlQuery q(m_db);
    bool ok = !built.hash.isEmpty();
    if (ok) {
        q.prepare("INSERT INTO aeat_records (environment, kind, issuer_nif, invoice_number, issue_date, total, hash,"
                  " xml, generated_at, state) VALUES (:e, :k, :n, :num, :d, :t, :h, :x, :g, :s)");
        q.bindValue(":e", m_environment);
        q.bindValue(":k", kindText(kind));
        q.bindValue(":n", issuerNif);
        q.bindValue(":num", text(built.invoiceNumber));
        q.bindValue(":d", built.issueDate.toString(kDateFormat));
        q.bindValue(":t", built.total);
        q.bindValue(":h", built.hash);
        q.bindValue(":x", text(built.xml));
        q.bindValue(":g", built.generatedAt.toString(Qt::ISODate));
        q.bindValue(":s", kPending);
        ok = q.exec();
    }
    const qint64 id = ok ? q.lastInsertId().toLongLong() : 0;
    if (ok) {
        q.prepare("INSERT OR REPLACE INTO aeat_chain (environment, issuer_nif, invoice_number, issue_date, hash, generated_at) "
                  "VALUES (:e, :n, :num, :d, :h, :g)");
        q.bindValue(":e", m_environment);
        q.bindValue(":n", issuerNif);
        q.bindValue(":num", built.invoiceNumber);
        q.bindValue(":d", built.issueDate.toString(kDateFormat));
        q.bindValue(":h", built.hash);
        q.bindValue(":g", AeatHash::timestampText(built.generatedAt));
        ok = q.exec();
    }
    if (ok)
        ok = m_db.commit();
    if (!ok) {
        m_lastError = q.lastError().isValid() ? q.lastError().text() : m_db.lastError().text();
        qWarning() << "AeatStore::append: record not stored -" << m_lastError;
        m_db.rollback();
        close(wasOpen);
        return stored;
    }
    close(wasOpen);
    stored = latest(kind, built.invoiceNumber);
    Q_ASSERT(stored.id == id);
    return stored;
}

AeatStore::Record AeatStore::readRecord(QSqlQuery &q) const
{
    Record r;
    r.id               = q.value(0).toLongLong();
    r.kind             = q.value(1).toString() == QLatin1String("alta") ? Kind::Registration : Kind::Cancellation;
    r.issuerNif        = q.value(2).toString();
    r.invoiceNumber    = q.value(3).toString();
    r.issueDate        = QDate::fromString(q.value(4).toString(), kDateFormat);
    r.total            = q.value(5).toDouble();
    r.hash             = q.value(6).toString();
    r.xml              = q.value(7).toString();
    r.generatedAt      = QDateTime::fromString(q.value(8).toString(), Qt::ISODate);
    r.state            = q.value(9).toString();
    r.csv              = q.value(10).toString();
    r.errorCode        = q.value(11).toString();
    r.errorDescription = q.value(12).toString();
    r.attempts         = q.value(13).toInt();
    return r;
}

AeatStore::Record AeatStore::latest(Kind kind, const QString &invoiceNumber)
{
    Record r;
    bool wasOpen = false;
    if (!open(&wasOpen))
        return r;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT %1 FROM aeat_records WHERE environment = :e AND invoice_number = :num"
                             " AND kind = :k ORDER BY id DESC LIMIT 1").arg(QLatin1String(kRecordColumns)));
    q.bindValue(":e", m_environment);
    q.bindValue(":num", invoiceNumber);
    q.bindValue(":k", kindText(kind));
    if (q.exec() && q.next())
        r = readRecord(q);
    q.finish();
    close(wasOpen);
    return r;
}

QList<AeatStore::Record> AeatStore::pending(int limit)
{
    QList<Record> out;
    bool wasOpen = false;
    if (!open(&wasOpen))
        return out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT %1 FROM aeat_records WHERE environment = :e AND state = :s ORDER BY id LIMIT :l")
                  .arg(QLatin1String(kRecordColumns)));
    q.bindValue(":e", m_environment);
    q.bindValue(":s", kPending);
    q.bindValue(":l", limit);
    if (q.exec())
        while (q.next())
            out << readRecord(q);
    q.finish();
    close(wasOpen);
    return out;
}

bool AeatStore::markAttempt(qint64 id)
{
    bool wasOpen = false;
    if (!open(&wasOpen))
        return false;
    QSqlQuery q(m_db);
    q.prepare("UPDATE aeat_records SET attempts = attempts + 1, last_attempt = :t WHERE id = :id");
    q.bindValue(":t", QDateTime::currentDateTime().toString(Qt::ISODate));
    q.bindValue(":id", id);
    const bool ok = q.exec() && q.numRowsAffected() == 1;
    close(wasOpen);
    return ok;
}

bool AeatStore::markOutcome(qint64 id, const QString &state, const QString &csv,
                            const QString &errorCode, const QString &errorDescription)
{
    bool wasOpen = false;
    if (!open(&wasOpen))
        return false;
    QSqlQuery q(m_db);
    q.prepare("UPDATE aeat_records SET state = :s, csv = :c, error_code = :ec, error_description = :ed WHERE id = :id");
    q.bindValue(":s", state);
    q.bindValue(":c", text(csv));
    q.bindValue(":ec", text(errorCode));
    q.bindValue(":ed", text(errorDescription));
    q.bindValue(":id", id);
    const bool ok = q.exec() && q.numRowsAffected() == 1;
    if (!ok)
        qWarning() << "AeatStore::markOutcome failed for record" << id << "-" << q.lastError().text();
    close(wasOpen);
    return ok;
}
