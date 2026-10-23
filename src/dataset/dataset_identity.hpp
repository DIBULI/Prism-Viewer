#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonParseError>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

namespace prism_viewer::dataset {

struct DatasetIdentity {
  std::string model = "41M3";
  bool defaulted = true;
  bool raw12_recording = false;
};

// Additive metadata: never infer a model from directory names or image sizes.
// Missing/empty declarations retain the legacy product interpretation (41M3).
// A4L is a read-compatible historical alias; never rewrite an existing dataset.
inline DatasetIdentity readDatasetIdentity(const std::filesystem::path& root) {
  DatasetIdentity result;
  const auto path = root / "dataset.info";
  if (!std::filesystem::exists(path)) return result;
  if (!std::filesystem::is_regular_file(path))
    throw std::runtime_error("dataset.info is not a regular file");
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("Cannot read dataset.info");
  constexpr int limit = 1024 * 1024;
  QByteArray bytes(limit + 1, '\0');
  input.read(bytes.data(), bytes.size());
  if (input.bad() || input.gcount() > limit)
    throw std::runtime_error("Unreadable or oversized dataset.info");
  bytes.resize(static_cast<int>(input.gcount()));
  if (bytes.startsWith("\xef\xbb\xbf")) bytes.remove(0,3);
  bytes = bytes.trimmed();
  QString declared, legacy;
  if (bytes.startsWith('{')) {
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
      throw std::runtime_error("Invalid JSON dataset.info");
    const auto object = doc.object();
    const auto field = [&object](const char* key) {
      const auto value = object.value(QLatin1String(key));
      if (!value.isUndefined() && !value.isString())
        throw std::runtime_error("Invalid dataset model field type");
      return value.toString().trimmed();
    };
    declared = field("device_model");
    legacy = field("model");
    result.raw12_recording = object.value(QStringLiteral("codec")).toString() ==
                            QStringLiteral("raw12-rice-lossless");
  } else {
    std::istringstream lines(bytes.toStdString());
    std::map<std::string, QString> models;
    for (std::string line; std::getline(lines, line);) {
      const auto value = QString::fromStdString(line).trimmed();
      if (value.startsWith('#')) continue;
      const auto separator = value.indexOf('=');
      if (separator < 0) continue;  // Full format validation is separate.
      const auto key = value.left(separator).trimmed().toStdString();
      if (key == "codec") result.raw12_recording =
          value.mid(separator + 1).trimmed() == QStringLiteral("raw12-rice-lossless");
      if (key != "device_model" && key != "model") continue;
      if (!models.emplace(key, value.mid(separator + 1).trimmed()).second)
        throw std::runtime_error("Duplicate dataset model field");
    }
    declared = models["device_model"];
    legacy = models["model"];
  }
  const auto normalize = [](QString model) {
    if (model.compare(QStringLiteral("Prism-A4L"), Qt::CaseInsensitive) == 0 ||
        model.compare(QStringLiteral("A4L"), Qt::CaseInsensitive) == 0 ||
        model.compare(QStringLiteral("Prism-41M3"), Qt::CaseInsensitive) == 0 ||
        model.compare(QStringLiteral("41M3"), Qt::CaseInsensitive) == 0)
      return QStringLiteral("41M3");
    if (model.compare(QStringLiteral("Prism-415M"), Qt::CaseInsensitive) == 0 ||
        model.compare(QStringLiteral("415M"), Qt::CaseInsensitive) == 0)
      return QStringLiteral("415M");
    if (model.size() > 64) throw std::runtime_error("Dataset model is too long");
    for (const auto c : model)
      if (!(c.isLetterOrNumber() || c == '-' || c == '_' || c == '.'))
        throw std::runtime_error("Invalid dataset model name");
    return model;  // Preserve unknown explicit models instead of relabeling A4L.
  };
  declared = normalize(declared);
  legacy = normalize(legacy);
  if (!declared.isEmpty() && !legacy.isEmpty() && declared != legacy)
    throw std::runtime_error("Conflicting dataset model fields");
  const auto model = declared.isEmpty() ? legacy : declared;
  if (!model.isEmpty()) {
    result.model = model.toStdString();
    result.defaulted = false;
  }
  return result;
}

}  // namespace prism_viewer::dataset
