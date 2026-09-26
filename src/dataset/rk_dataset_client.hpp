#pragma once
#include "prism/usb/datasets.hpp"
#include <QtCore/QJsonArray>
#include <QtCore/QString>
#include <functional>

namespace prism_viewer::dataset {
using RkDownloadCancelled = prism::DatasetDownloadCancelled;
using RkDownloadCancel = prism::DatasetCancel;
using RkDownloadProgress = std::function<void(quint64, quint64, const QString&)>;
// Supplied by MainWindow's existing serialized USB connection. No HTTP fallback.
struct RkDatasetAccess {
  std::function<std::vector<prism::RecordedDataset>()> list;
  std::function<std::string(const std::string&, const std::string&,
                           const prism::DatasetProgress&, const prism::DatasetCancel&)> download;
};
QJsonArray listRkDatasets(const RkDatasetAccess&, const RkDownloadCancel& cancel = {});
QString downloadRkDataset(const RkDatasetAccess&, const QString& name, const QString& parent,
                         const RkDownloadProgress& progress = {}, const RkDownloadCancel& cancel = {});
}
