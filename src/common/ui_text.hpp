#pragma once

#include <QtCore/QString>

#include <filesystem>
#include <string>

namespace prism_viewer::common {

void setChineseUi(bool enabled);
bool chineseUi();

QString uiText(const char* english, const char* chinese);
QString wideToQString(const std::wstring& text);
QString toQString(const std::string& text);
std::filesystem::path toFilesystemPath(const QString& path);
QString fromFilesystemPath(const std::filesystem::path& path);

}  // namespace prism_viewer::common
