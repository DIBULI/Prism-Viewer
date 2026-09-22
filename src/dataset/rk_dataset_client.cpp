#include "dataset/rk_dataset_client.hpp"
#include <QtCore/QDir>
#include <QtCore/QEventLoop>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QSet>
#include <QtCore/QStorageInfo>
#include <QtCore/QTimer>
#include <QtCore/QUuid>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <cmath>
#include <limits>
#include <memory>

namespace prism_viewer::dataset {
namespace {
const QRegularExpression namePattern(QStringLiteral("\\Acapture-[A-Za-z0-9_-]{1,120}\\z"));
const QRegularExpression filePattern(QStringLiteral(
    "\\A(?:manifest\\.json|dataset\\.info|cam[0-3]\\.tum|imu[01]\\.tum|lidar(?:_imu)?\\.tum|"
    "camera_metadata\\.csv|imu_metadata\\.csv|events\\.csv|(?:camera|events)-data-[0-9]{4,12}\\.bin)\\z"));
[[noreturn]] void fail(const QString& text) { throw std::runtime_error(text.toUtf8().constData()); }
void checkCancel(const RkDownloadCancel& cancel) { if (cancel && cancel()) throw RkDownloadCancelled(); }
quint64 sizeValue(const QJsonValue& value) {
  const double n = value.toDouble(-1);
  if (!value.isDouble() || !std::isfinite(n) || n < 0 || n > 9007199254740991.0 || std::floor(n) != n)
    fail(QStringLiteral("Invalid recording file size"));
  return static_cast<quint64>(n);
}

// Called in the dialog's worker thread. Network, disk IO and ROS export do
// not run on the GUI thread; reads stay bounded even for multi-GiB chunks.
QByteArray request(QNetworkAccessManager& network, const QUrl& url,
                   QFile* output, quint64 expected_size, const QByteArray& etag,
                   const std::function<void(quint64)>& progress,
                   const RkDownloadCancel& cancel) {
  checkCancel(cancel);
  QNetworkRequest req(url);
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
  req.setRawHeader("Accept-Encoding", "identity");
  if (!etag.isEmpty()) req.setRawHeader("If-Match", etag);
  std::unique_ptr<QNetworkReply> reply(network.get(req));
  reply->setReadBufferSize(256 * 1024);
  QEventLoop loop;
  QTimer timeout, poll;
  timeout.setSingleShot(true); timeout.setInterval(15000);
  poll.setInterval(100);
  QString error;
  QByteArray body;
  quint64 received = 0;
  const auto abort = [&](const QString& reason) { if (error.isEmpty()) error=reason; reply->abort(); };
  const auto consume = [&] {
    if (!error.isEmpty()) return;
    if (output && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) return;
    while (reply->bytesAvailable()) {
      QByteArray block = reply->read(256 * 1024);
      if (block.isEmpty()) break;
      received += static_cast<quint64>(block.size());
      if (received > (output ? expected_size : 4 * 1024 * 1024ULL)) {
        abort(QStringLiteral("Response larger than declared recording size")); return;
      }
      if (output) {
        if (output->write(block) != block.size()) { abort(QStringLiteral("Local disk write failed: ")+output->errorString()); return; }
      } else body += block;
      timeout.start();
      if (progress) progress(received);
    }
  };
  QObject::connect(reply.get(), &QNetworkReply::readyRead, &loop, consume);
  QObject::connect(reply.get(), &QNetworkReply::finished, &loop, &QEventLoop::quit);
  QObject::connect(&timeout, &QTimer::timeout, &loop, [&] { abort(QStringLiteral("RK request timed out (15 seconds without data)")); });
  QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (cancel && cancel()) reply->abort(); });
  timeout.start(); poll.start();
  if (!reply->isFinished()) loop.exec();
  consume();
  checkCancel(cancel);
  if (!error.isEmpty()) fail(error);
  const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  if (status != 200) {
    const auto json = QJsonDocument::fromJson(body).object();
    fail(QStringLiteral("RK HTTP %1: %2").arg(status).arg(json.value(QStringLiteral("error")).toString(reply->errorString())));
  }
  if (reply->error() != QNetworkReply::NoError) fail(reply->errorString());
  if (output && (received != expected_size || reply->rawHeader("ETag") != etag ||
                 reply->rawHeader("Content-Length") != QByteArray::number(expected_size)))
    fail(QStringLiteral("Incomplete or changed RK recording file"));
  return body;
}
QJsonDocument jsonRequest(QNetworkAccessManager& network, const QUrl& url, const RkDownloadCancel& cancel) {
  QJsonParseError error;
  auto doc = QJsonDocument::fromJson(request(network, url, nullptr, 0, {}, {}, cancel), &error);
  if (error.error != QJsonParseError::NoError) fail(QStringLiteral("Invalid RK JSON response"));
  return doc;
}
}  // namespace

QUrl rkDatasetEndpoint(const QString& text) {
  QString input = text.trimmed();
  if (!input.contains(QStringLiteral("://"))) input.prepend(QStringLiteral("http://"));
  QUrl url(input, QUrl::StrictMode);
  QHostAddress address;
  if (!url.isValid() || url.scheme() != QStringLiteral("http") || !address.setAddress(url.host()) ||
      !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment() ||
      (!url.path().isEmpty() && url.path() != QStringLiteral("/")) || url.port(8080) < 1)
    fail(QStringLiteral("Use an RK IP address, for example http://10.42.200.1:8080"));
  if (url.port() == -1) url.setPort(8080);
  url.setPath(QStringLiteral("/"));
  return url;
}

QJsonArray listRkDatasets(const QUrl& base, const RkDownloadCancel& cancel) {
  rkDatasetEndpoint(base.toString());
  QNetworkAccessManager network;
  auto doc = jsonRequest(network, base.resolved(QUrl(QStringLiteral("/api/datasets"))), cancel);
  if (!doc.isArray()) fail(QStringLiteral("RK dataset list is not an array"));
  for (const auto& value : doc.array()) {
    if (!value.isObject() || !namePattern.match(value.toObject().value(QStringLiteral("name")).toString()).hasMatch())
      fail(QStringLiteral("Invalid RK dataset name"));
  }
  return doc.array();
}

QString downloadRkDataset(const QUrl& base, const QString& name, const QString& parent,
                          const RkDownloadProgress& progress, const RkDownloadCancel& cancel) {
  rkDatasetEndpoint(base.toString());
  if (!namePattern.match(name).hasMatch()) fail(QStringLiteral("Invalid RK dataset name"));
  QDir parentDir(parent);
  if (!parentDir.exists()) fail(QStringLiteral("Local destination folder does not exist"));
  const QString destination = parentDir.absoluteFilePath(name);
  if (QFileInfo::exists(destination)) fail(QStringLiteral("Dataset already exists; choose another destination: ")+destination);
  QNetworkAccessManager network;
  const QUrl inventoryUrl = base.resolved(QUrl(QStringLiteral("/api/datasets/")+name+QStringLiteral("/files")));
  auto doc = jsonRequest(network, inventoryUrl, cancel);
  auto inventory = doc.object();
  if (!doc.isObject() || inventory.value(QStringLiteral("name")).toString() != name ||
      inventory.value(QStringLiteral("format")).toString() != QStringLiteral("prism-web-dataset-v1") ||
      !inventory.value(QStringLiteral("files")).isArray())
    fail(QStringLiteral("Unsupported RK recording inventory; update the RK web service"));
  const auto files = inventory.value(QStringLiteral("files")).toArray();
  QSet<QString> names;
  quint64 total = 0;
  for (const auto& entry : files) {
    const auto file = entry.toObject();
    const QString filename = file.value(QStringLiteral("name")).toString();
    const auto etag = file.value(QStringLiteral("etag")).toString();
    if (!entry.isObject() || !filePattern.match(filename).hasMatch() || names.contains(filename) ||
        etag.size() > 128 || !etag.startsWith('"') || !etag.endsWith('"') || etag.contains('\r') || etag.contains('\n'))
      fail(QStringLiteral("Unsafe or duplicate RK recording file"));
    names.insert(filename);
    const auto n = sizeValue(file.value(QStringLiteral("size")));
    if (n > 9007199254740991ULL - total) fail(QStringLiteral("Recording is too large"));
    total += n;
  }
  if (!names.contains(QStringLiteral("manifest.json")) || total != sizeValue(inventory.value(QStringLiteral("total_bytes"))))
    fail(QStringLiteral("Incomplete RK file inventory"));
  QStorageInfo storage(parentDir.absolutePath());
  if (storage.isValid() && storage.bytesAvailable() >= 0 &&
      static_cast<quint64>(storage.bytesAvailable()) < total + 64 * 1024 * 1024ULL)
    fail(QStringLiteral("Insufficient local disk space for the recording"));
  checkCancel(cancel);
  const QString partial = parentDir.absoluteFilePath(name + QStringLiteral(".partial-") + QUuid::createUuid().toString(QUuid::WithoutBraces));
  if (!parentDir.mkdir(QFileInfo(partial).fileName())) fail(QStringLiteral("Cannot create download folder"));
  quint64 completed = 0;
  try {
    for (const auto& entry : files) {
      checkCancel(cancel);
      const auto metadata = entry.toObject();
      const auto filename = metadata.value(QStringLiteral("name")).toString();
      const auto size = sizeValue(metadata.value(QStringLiteral("size")));
      QFile file(QDir(partial).filePath(filename));
      if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) fail(file.errorString());
      request(network, QUrl(inventoryUrl.toString()+QStringLiteral("/")+filename), &file, size,
              metadata.value(QStringLiteral("etag")).toString().toLatin1(),
              [&](quint64 n) { if (progress) progress(completed+n, total, filename); }, cancel);
      if (!file.flush()) fail(file.errorString());
      file.close(); completed += size;
    }
    // Recheck the snapshot after downloading, including manifest version.
    if (jsonRequest(network, inventoryUrl, cancel).object() != inventory)
      fail(QStringLiteral("RK dataset changed during download; refresh and retry"));
    checkCancel(cancel);
    if (QFileInfo::exists(destination) || !QDir().rename(partial, destination))
      fail(QStringLiteral("Cannot finalize downloaded dataset without overwriting another folder"));
  } catch (const RkDownloadCancelled&) {
    throw;  // Deliberately retain .partial data; never delete user recordings.
  } catch (const std::exception& error) {
    fail(QString::fromUtf8(error.what()) + QStringLiteral("\nPartial download retained: ") + partial);
  }
  if (progress) progress(total, total, name);
  return destination;
}
}  // namespace prism_viewer::dataset
