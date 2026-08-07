#include "common/ui_text.hpp"

#include <atomic>

namespace prism_viewer::common {
namespace {

std::atomic<bool> g_chinese_ui{false};

}  // namespace

void setChineseUi(bool enabled) {
  g_chinese_ui.store(enabled, std::memory_order_relaxed);
}

bool chineseUi() {
  return g_chinese_ui.load(std::memory_order_relaxed);
}

QString uiText(const char* english, const char* chinese) {
  return QString::fromUtf8(chineseUi() ? chinese : english);
}

QString wideToQString(const std::wstring& text) {
  return QString::fromWCharArray(text.c_str(), static_cast<int>(text.size()));
}

QString toQString(const std::string& text) {
  return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}

std::filesystem::path toFilesystemPath(const QString& path) {
#ifdef _WIN32
  return std::filesystem::path(path.toStdWString());
#else
  return std::filesystem::path(path.toUtf8().constData());
#endif
}

QString fromFilesystemPath(const std::filesystem::path& path) {
#ifdef _WIN32
  return QString::fromStdWString(path.wstring());
#else
  return QString::fromUtf8(path.string().c_str());
#endif
}

}  // namespace prism_viewer::common
