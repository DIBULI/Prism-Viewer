#pragma once
#include <QByteArray>
#include <QString>
#include <QUrl>

namespace prism_viewer::update {
struct Release {
  QString version, revision, name, notes;
  qint64 epoch=0, size=0;
  QByteArray sha256;
  QUrl url;
};
QString platform();
bool trustedUrl(const QUrl& url);
QUrl manifestUrl(const QByteArray& json);
Release parseManifest(const QByteArray& json, const QString& platform);
bool newer(const Release&, const QString& version, qint64 epoch, const QString& revision);
} // namespace prism_viewer::update
