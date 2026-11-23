#include "update/cleanup.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <algorithm>

namespace prism_viewer::update {
namespace {
bool link(const QFileInfo& info) {
#ifdef Q_OS_WIN
  return info.isSymLink() || info.isJunction();
#else
  return info.isSymLink();
#endif
}
bool safeRelative(const QString& path) {
  if (path.isEmpty() || path.contains('\\') || path.contains(':') || path.contains(QChar(0)) ||
      QDir::isAbsolutePath(path)) return false;
  for (const auto& part : path.split('/'))
    if (part.isEmpty() || part == "." || part == ".." || part.endsWith('.') || part.endsWith(' ')) return false;
  return true;
}
bool parentsAreDirectories(const QString& root, const QString& relative) {
  auto parts = relative.split('/'); parts.removeLast();
  QString parent = root;
  for (const auto& part : parts) {
    parent += '/' + part;
    const QFileInfo info(parent);
    if (link(info) || !info.isDir()) return false;
  }
  return true;
}
QByteArray readSmall(const QString& path, qint64 limit) {
  QFile file(path);
  if (link(QFileInfo(path)) || !file.open(QIODevice::ReadOnly) || file.size() > limit) return {};
  return file.readAll();
}
}

int cleanupPreviousPackage(const QString& installedRoot, const QString& backupPath,
                           const QString& workPath) {
  const QFileInfo target(installedRoot), backup(backupPath), work(workPath);
  const QString prefix = target.fileName() + ".backup-";
  const QString token = backup.fileName().mid(prefix.size());
  if (!target.isDir() || link(target) || !backup.isDir() || link(backup) ||
      !work.isDir() || link(work) || !work.fileName().startsWith("prism-viewer-update-") ||
      backup.dir().canonicalPath() != target.dir().canonicalPath() ||
      !backup.fileName().startsWith(prefix) ||
      !QRegularExpression("^[A-Za-z0-9]{6}$").match(token).hasMatch() ||
      readSmall(work.filePath() + "/result", 32) != "installed" ||
      readSmall(work.filePath() + "/started", 32) != "started") return 2;
  const QString root = backup.canonicalFilePath();
  const QString manifest = target.fileName().endsWith(".app")
      ? "Contents/Resources/PRISM_PACKAGE_FILES.json" : "PRISM_PACKAGE_FILES.json";
  if (!parentsAreDirectories(root, manifest)) return 3;
  const auto object = QJsonDocument::fromJson(readSmall(root + '/' + manifest, 8 * 1024 * 1024)).object();
  if (object.value("format").toInt() != 1 || object.value("product") != "Prism Viewer" ||
      !object.value("files").isArray() || !object.value("directories").isArray()) return 3;
  QStringList files, directories;
  QSet<QString> paths;
  for (const auto& field : {QStringLiteral("files"), QStringLiteral("directories")}) {
    const auto array = object.value(field).toArray();
    if (array.size() > 100000) return 3;
    for (const auto& entry : array) {
      const QString path = entry.toString();
      if (!entry.isString() || !safeRelative(path) || paths.contains(path)) return 3;
      paths.insert(path);
      (field == "files" ? files : directories).append(path);
    }
  }
  if (!files.removeOne(manifest)) return 3;
  int removed = 0;
  bool deferred = false;
  for (const auto& relative : files) {
    const QString path = root + '/' + relative;
    const QFileInfo info(path);
    if (!info.exists() && !link(info)) continue;
    // Do not traverse symlinks/junctions or recursively delete any directory.
    if (!parentsAreDirectories(root, relative) || (!link(info) && !info.isFile()) ||
        !QFile::remove(path)) { deferred = true; continue; }
    ++removed;
  }
  if (!deferred && !QFile::remove(root + '/' + manifest)) deferred = true;
  std::sort(directories.begin(), directories.end(), [](const QString& a, const QString& b) {
    return a.size() > b.size();
  });
  for (const auto& relative : directories) {
    const QString path = root + '/' + relative;
    if (parentsAreDirectories(root, relative) && !link(QFileInfo(path))) QDir().rmdir(path);
  }
  QDir().rmdir(root); // Only when no personal files remain.
  const QString stage = target.dir().filePath(".prism-viewer-stage-" + token);
  if (!link(QFileInfo(stage))) {
    if (!link(QFileInfo(stage + "/extract"))) QDir().rmdir(stage + "/extract");
    QDir().rmdir(stage); // Never recurse into staging leftovers.
  }
  QSaveFile report(work.filePath() + "/cleanup.json");
  if (report.open(QIODevice::WriteOnly)) {
    report.write(QJsonDocument(QJsonObject{
        {"status", deferred ? "deferred" : QFileInfo::exists(root) ? "personal-files-preserved" : "cleaned"},
        {"removed_files", removed}, {"backup_remains", QFileInfo::exists(root)}}).toJson());
    report.commit();
  }
  return deferred ? 4 : 0;
}
}
