#pragma once
#include <QtCore/QJsonArray>
#include <QtCore/QString>
#include <QtCore/QUrl>
#include <functional>
#include <stdexcept>

namespace prism_viewer::dataset {
class RkDownloadCancelled : public std::runtime_error {
 public:
  RkDownloadCancelled() : std::runtime_error("Download cancelled") {}
};
using RkDownloadCancel = std::function<bool()>;
using RkDownloadProgress = std::function<void(quint64, quint64, const QString&)>;
QUrl rkDatasetEndpoint(const QString& text);
QJsonArray listRkDatasets(const QUrl& base, const RkDownloadCancel& cancel = {});
// Creates a fresh child directory. A failure leaves a clearly named .partial
// directory; an existing dataset is never replaced. Does not stop/delete RK data.
QString downloadRkDataset(const QUrl& base, const QString& name, const QString& parent,
                          const RkDownloadProgress& progress = {},
                          const RkDownloadCancel& cancel = {});
}  // namespace prism_viewer::dataset
