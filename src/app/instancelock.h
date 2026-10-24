#ifndef INSTANCELOCK_H
#define INSTANCELOCK_H

// One copy of the app per database: two would hand out the same ticket number (MAX + 1
// twice) and, with the direct client, fork the AEAT hash chain. The lock lives in the
// user's temp folder, keyed on the database path - not next to the database, which may
// be synced to other PCs.

#include <QLockFile>
#include <QString>
#include <memory>

namespace InstanceLock {

// The lock file for `dbPath` (case-insensitive and separator-insensitive, as on Windows).
QString lockPath(const QString &dbPath);

// The held lock, or null when another running copy holds it. Waits up to `waitMs` for a
// copy that is closing; a lock left by a copy that crashed is taken over.
std::unique_ptr<QLockFile> acquire(const QString &dbPath, int waitMs = 3000);

} // namespace InstanceLock

#endif // INSTANCELOCK_H
