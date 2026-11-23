#pragma once
#include <QString>

namespace prism_viewer::update {
// Only this transaction's distribution files, after successful startup.
int cleanupPreviousPackage(const QString& installedRoot, const QString& backup,
                           const QString& work);
}
