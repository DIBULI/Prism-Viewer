#include "update/cleanup.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QMap>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QThread>
#include <QTimer>
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
bool writeJson(const QString& path, const QJsonObject& value) {
  if (link(QFileInfo(path))) return false;
  QSaveFile file(path);
  return file.open(QIODevice::WriteOnly) &&
      file.write(QJsonDocument(value).toJson()) > 0 && file.commit();
}
bool committed(const QString& work) {
  const QFileInfo info(work);
  return info.isDir() && !link(info) &&
      (info.fileName().startsWith("prism-viewer-update-") ||
       info.fileName().startsWith(".prism-viewer-update-")) &&
      readSmall(work + "/result", 32) == "installed" &&
      readSmall(work + "/started", 32) == "started";
}
bool validRoot(const QFileInfo& target) {
  return target.isDir() && !link(target) &&
      target.absoluteFilePath() == target.canonicalFilePath() &&
      !target.fileName().startsWith(".prism-viewer-stage-") &&
      !QRegularExpression("\\.backup-[A-Za-z0-9]{6}$").match(target.fileName()).hasMatch();
}
QString backupToken(const QFileInfo& target, const QFileInfo& backup) {
  const QString prefix = target.fileName() + ".backup-";
  const QString token = backup.fileName().mid(prefix.size());
  if (link(backup) || backup.dir().canonicalPath() != target.dir().canonicalPath() ||
      !backup.fileName().startsWith(prefix) ||
      !QRegularExpression("^[A-Za-z0-9]{6}$").match(token).hasMatch()) return {};
  return token;
}
void emptyStage(const QDir& parent, const QString& token) {
  const QString stage = parent.filePath(".prism-viewer-stage-" + token);
  if (!link(QFileInfo(stage))) {
    if (!link(QFileInfo(stage + "/extract"))) QDir().rmdir(stage + "/extract");
    QDir().rmdir(stage); // Empty directories only; never erase failed packages.
  }
}
QString receiptPath(const QString& root) {
  const auto id = QCryptographicHash::hash(root.toUtf8(), QCryptographicHash::Sha256).toHex();
  return QDir::tempPath() + "/prism-viewer-update-receipts/" + QString::fromLatin1(id) + ".json";
}
QString installedRoot() {
  QDir root(QCoreApplication::applicationDirPath());
#ifdef Q_OS_MACOS
  root.cdUp(); root.cdUp();
#elif !defined(Q_OS_WIN)
  root.cdUp();
#endif
  return root.absolutePath();
}
struct Inventory {
  QStringList files, directories;
  QMap<QString, QByteArray> hashes;
  QString manifest;
};
bool inventory(const QString& root, bool bundle, Inventory& out) {
  out.manifest = bundle ? "Contents/Resources/PRISM_PACKAGE_FILES.json" : "PRISM_PACKAGE_FILES.json";
  if (QFileInfo::exists(root + '/' + out.manifest) || link(QFileInfo(root + '/' + out.manifest))) {
    if (!parentsAreDirectories(root, out.manifest)) return false;
    const auto object = QJsonDocument::fromJson(readSmall(root + '/' + out.manifest, 8 * 1024 * 1024)).object();
    if (object.value("format").toInt() != 1 || object.value("product") != "Prism Viewer" ||
        !object.value("files").isArray() || !object.value("directories").isArray()) return false;
    QSet<QString> paths;
    for (const auto& field : {QStringLiteral("files"), QStringLiteral("directories")}) {
      const auto array = object.value(field).toArray();
      if (array.size() > 100000) return false;
      for (const auto& entry : array) {
        const QString path = entry.toString();
        if (!entry.isString() || !safeRelative(path) || paths.contains(path)) return false;
        paths.insert(path);
        (field == "files" ? out.files : out.directories).append(path);
      }
    }
    return out.files.removeOne(out.manifest);
  }
  // Older Windows/Linux packages have checksums but no inventory. Delete only
  // matching program files; preserve modified files and all user additions.
  out.manifest = "SHA256SUMS.txt";
  QString checksums = QString::fromUtf8(readSmall(root + '/' + out.manifest, 8 * 1024 * 1024));
  if (checksums.startsWith(QChar(0xfeff))) checksums.remove(0, 1);
  QSet<QString> directories;
  const QRegularExpression pattern("^([A-Fa-f0-9]{64}) [ *](.+)$");
  for (QString line : checksums.split('\n')) {
    if (line.endsWith('\r')) line.chop(1);
    if (line.isEmpty()) continue;
    const auto match = pattern.match(line);
    if (!match.hasMatch()) return false;
    QString path = match.captured(2);
    path.replace('\\', '/');
    if (path.startsWith("./")) path.remove(0, 2);
    if (!safeRelative(path) || path == out.manifest || out.hashes.contains(path) || out.files.size() >= 100000) return false;
    out.files.append(path);
    out.hashes.insert(path, match.captured(1).toLatin1().toLower());
    auto parts = path.split('/'); parts.removeLast();
    while (!parts.isEmpty()) { directories.insert(parts.join('/')); parts.removeLast(); }
  }
  out.directories = directories.values();
  return out.hashes.contains("prism-viewer.exe") || out.hashes.contains("bin/prism-viewer");
}
int cleanPackage(const QFileInfo& target, const QFileInfo& backup, QJsonObject& report) {
  const QString token = backupToken(target, backup);
  if (token.isEmpty()) return 2;
  emptyStage(target.dir(), token);
  if (!backup.exists()) return 0;
  if (!backup.isDir()) return 2;
  const QString root = backup.canonicalFilePath();
  Inventory package;
  if (!inventory(root, target.fileName().endsWith(".app"), package)) return 3;
  int removed = 0;
  bool deferred = false;
  for (const auto& relative : package.files) {
    const QString path = root + '/' + relative;
    const QFileInfo info(path);
    if (!info.exists() && !link(info)) continue;
    // Do not traverse symlinks/junctions or recursively delete any directory.
    if (!parentsAreDirectories(root, relative) || (!link(info) && !info.isFile())) { deferred = true; continue; }
    if (!package.hashes.isEmpty()) {
      if (link(info)) { deferred = true; continue; }
      QFile file(path);
      QCryptographicHash hash(QCryptographicHash::Sha256);
      if (info.size() > 512LL * 1024 * 1024 || !file.open(QIODevice::ReadOnly) || !hash.addData(&file)) { deferred = true; continue; }
      if (hash.result().toHex() != package.hashes.value(relative)) continue;
      file.close();
    }
    if (!QFile::remove(path)) { deferred = true; continue; }
    ++removed;
  }
  if (!deferred && !QFile::remove(root + '/' + package.manifest)) deferred = true;
  std::sort(package.directories.begin(), package.directories.end(), [](const QString& a, const QString& b) {
    return a.size() > b.size();
  });
  for (const auto& relative : package.directories) {
    const QString path = root + '/' + relative;
    if (parentsAreDirectories(root, relative) && !link(QFileInfo(path))) QDir().rmdir(path);
  }
  QDir().rmdir(root); // Only when no personal files remain.
  report = {{"status", deferred ? "deferred" : QFileInfo::exists(root) ? "personal-files-preserved" : "cleaned"},
            {"removed_files", removed}, {"backup_remains", QFileInfo::exists(root)}};
  return deferred ? 4 : 0;
}

}
int cleanupPreviousPackage(const QString& installed, const QString& backup, const QString& work) {
  const QFileInfo target(installed);
  if (!validRoot(target) || !committed(work)) return 2;
  if (!QDir().mkpath(QFileInfo(receiptPath(installed)).absolutePath())) return 4;
  QLockFile lock(receiptPath(installed) + ".lock");
  if (!lock.tryLock(1000)) return 4;
  QJsonObject report;
  const int result = cleanPackage(target, QFileInfo(backup), report);
  if (!report.isEmpty()) writeJson(work + "/cleanup.json", report);
  return result;
}

int finishUpdateCleanup(const QString& installed, const QString& work) {
  const QFileInfo target(installed);
  if (!validRoot(target)) return 2;
  const auto receipt = receiptPath(installed);
  if (!work.isEmpty()) {
    if (!committed(work)) return 2;
  } else {
    const auto previous = QJsonDocument::fromJson(readSmall(receipt, 32768)).object();
    if (previous.value("format").toInt() != 1 || previous.value("installed_root").toString() != installed ||
        !previous.value("committed").toBool()) return 2;
  }
  if (!QDir().mkpath(QFileInfo(receipt).absolutePath())) return 4;
  QLockFile lock(receiptPath(installed) + ".lock");
  if (!lock.tryLock(1000)) return 4;
  QJsonObject evidence{{"format", 1}, {"installed_root", installed}, {"committed", true}};
  if (!writeJson(receipt, evidence)) return 4;
  int result = 0;
  QJsonArray reports;
  // Outside the GUI: short retries for transient Windows handles, then another
  // bounded pass on the next healthy startup using the persisted commit.
  for (int attempt = 0; attempt < 3; ++attempt) {
    bool deferred = false;
    int count = 0;
    reports = {};
    const auto entries = target.dir().entryInfoList(QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot);
    for (const auto& backup : entries) {
      if (backupToken(target, backup).isEmpty()) continue;
      if (++count > 32) break;
      QJsonObject report;
      const int code = cleanPackage(target, backup, report);
      report.insert("backup", backup.fileName()); report.insert("code", code);
      reports.append(report);
      deferred |= code == 4;
    }
    // Empty stages only; payloads from failed/prepared updates remain intact.
    for (const auto& stage : entries) {
      const auto match = QRegularExpression("^\\.prism-viewer-stage-([A-Za-z0-9]{6})$").match(stage.fileName());
      if (match.hasMatch()) emptyStage(target.dir(), match.captured(1));
    }
    result = deferred ? 4 : 0;
    if (!deferred) break;
    if (attempt < 2) QThread::msleep(1000);
  }
  evidence.insert("backups", reports); evidence.insert("deferred", result != 0);
  writeJson(receipt, evidence);
  if (!work.isEmpty()) writeJson(work + "/startup-cleanup.json", evidence);
  return result;
}

void scheduleUpdateCleanup(QObject* owner, const QString& acknowledgement) {
  const auto root = installedRoot();
  if (acknowledgement.isEmpty()) {
    if (!QFileInfo::exists(receiptPath(root))) return;
    QTimer::singleShot(3000, owner, [] {
      QProcess::startDetached(QCoreApplication::applicationFilePath(), {"--finish-update-cleanup", ""}, QDir::tempPath());
    });
    return;
  }
  const QString work = QFileInfo(acknowledgement).absolutePath();
  auto* timer = new QTimer(owner);
  timer->setInterval(250);
  QObject::connect(timer, &QTimer::timeout, owner, [timer, work, attempts = 0]() mutable {
    if (committed(work)) {
      timer->stop(); timer->deleteLater();
      QProcess::startDetached(QCoreApplication::applicationFilePath(), {"--finish-update-cleanup", work}, QDir::tempPath());
    } else if (++attempts >= 140 || readSmall(work + "/result", 32) == "rollback") {
      timer->stop(); timer->deleteLater();
    }
  });
  timer->start();
}
}
