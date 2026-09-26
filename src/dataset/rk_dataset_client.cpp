#include "dataset/rk_dataset_client.hpp"
#include <QtCore/QJsonObject>

namespace prism_viewer::dataset {
QJsonArray listRkDatasets(const RkDatasetAccess& access, const RkDownloadCancel& cancel) {
  if (cancel && cancel()) throw RkDownloadCancelled();
  if (!access.list) throw std::runtime_error("Connect the device over USB first");
  const auto datasets=access.list();
  if (cancel && cancel()) throw RkDownloadCancelled();
  QJsonArray rows;
  for(const auto& d:datasets) rows.append(QJsonObject{
    {"name",QString::fromStdString(d.name)},
    {"modified",QString::number(d.modified_unix_s)},
    {"manifest",QJsonObject{{"complete",d.complete},
       {"viewer_format",d.prism_v6?QStringLiteral("prism-dataset-v6"):QString()}}}});
  return rows;
}
QString downloadRkDataset(const RkDatasetAccess& access, const QString& name, const QString& parent,
                         const RkDownloadProgress& progress, const RkDownloadCancel& cancel) {
  if (!access.download) throw std::runtime_error("Connect the device over USB first");
  const auto path=access.download(name.toUtf8().toStdString(),parent.toUtf8().toStdString(),
    [&](const prism::DatasetDownloadProgress& p) {
      if(progress) progress(p.completed_bytes,p.total_bytes,QString::fromStdString(p.file));
    },cancel);
  return QString::fromUtf8(path.data(),static_cast<int>(path.size()));
}
}
