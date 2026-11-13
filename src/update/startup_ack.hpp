#pragma once
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>

namespace prism_viewer::update {
inline constexpr const char* kWorkDirectoryPrefix = "prism-viewer-update-";

inline bool writeStartupAcknowledgement(const QString& path) {
  const QFileInfo info(path);
  const auto directory = info.dir().dirName();
  // Older installed Viewers launch the new binary using the original hidden
  // work directory. Keep that bootstrap path compatible as well.
  if (info.fileName() != QStringLiteral("started") ||
      (!directory.startsWith(QLatin1String(kWorkDirectoryPrefix)) &&
       !directory.startsWith(QStringLiteral(".prism-viewer-update-")))) return false;
  QFile ack(path);
  if (!ack.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return false;
  const bool written = ack.write("started") == 7 && ack.flush();
  ack.close();
  return written;
}
}  // namespace prism_viewer::update
