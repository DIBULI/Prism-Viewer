#pragma once
#include <QString>
class QObject;

namespace prism_viewer::update {
// Only this transaction's distribution files, after successful startup.
int cleanupPreviousPackage(const QString& installedRoot, const QString& backup,
                           const QString& work);
// Finish older helpers' updates; retry committed cleanup on later startups.
int finishUpdateCleanup(const QString& installedRoot, const QString& work);
void scheduleUpdateCleanup(QObject* owner, const QString& acknowledgement = {});
}
