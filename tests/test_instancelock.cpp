// Qt Test suite for InstanceLock (src/app/instancelock.h): one copy of the app per
// database. Two would hand out the same ticket number and, with the direct client, fork
// the AEAT hash chain.

#include <QtTest>
#include <QFile>
#include <QSysInfo>
#include <QTemporaryDir>

#include "instancelock.h"

class TestInstanceLock : public QObject
{
    Q_OBJECT

private slots:
    // The same database under another spelling shares the lock; another database does not.
    void test_lockPath()
    {
        QCOMPARE(InstanceLock::lockPath("C:/Shop/LaIdeal.db"), InstanceLock::lockPath(R"(c:\shop\laideal.db)"));
        QVERIFY(InstanceLock::lockPath("C:/Shop/LaIdeal.db") != InstanceLock::lockPath("C:/Shop/copia.db"));
        QVERIFY(InstanceLock::lockPath("C:/Shop/LaIdeal.db").startsWith(QDir::tempPath()));
    }

    void test_secondCopyIsRefusedUntilTheFirstCloses()
    {
        QTemporaryDir dir;
        const QString db = dir.filePath("laideal.db");
        auto first = InstanceLock::acquire(db, 0);
        QVERIFY(first);
        QVERIFY(!InstanceLock::acquire(db, 100));
        QVERIFY(InstanceLock::acquire(dir.filePath("other.db"), 0));       // another database is free
        first.reset();                                                     // the first copy closes
        QVERIFY(InstanceLock::acquire(db, 0));
    }

    // A copy that crashed leaves its lock behind: the next start takes it over.
    void test_lockOfACrashedCopyIsTakenOver()
    {
        QTemporaryDir dir;
        const QString db = dir.filePath("crashed.db");
        // A real lock file of this machine, with the pid of a process that no longer exists.
        QByteArray content;
        {
            auto held = InstanceLock::acquire(db, 0);
            QVERIFY(held);
            QFile file(InstanceLock::lockPath(db));
            QVERIFY(file.open(QIODevice::ReadOnly));
            content = file.readAll();
        }                                                                  // released and removed
        content.replace(0, content.indexOf('\n'), "999999");
        QFile stale(InstanceLock::lockPath(db));
        QVERIFY(stale.open(QIODevice::WriteOnly));
        stale.write(content);
        stale.close();
        auto lock = InstanceLock::acquire(db, 0);
        QVERIFY(lock);
        lock.reset();
        QVERIFY(!QFile::exists(InstanceLock::lockPath(db)));
    }
};

QTEST_MAIN(TestInstanceLock)
#include "test_instancelock.moc"
