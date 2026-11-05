#pragma once
#include <QDialog>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QSaveFile>
#include <QCryptographicHash>
#include <QTimer>
#include "update/release.hpp"
#include "update/install.hpp"
class QLabel;
class QPushButton;
class QProgressBar;
class QPlainTextEdit;
class QNetworkReply;
namespace prism_viewer::ui {
class ViewerUpdateDialog final:public QDialog {
 public:
  explicit ViewerUpdateDialog(QWidget* parent,std::function<bool()> can_restart,QNetworkAccessManager* network=nullptr);
  ~ViewerUpdateDialog() override;
 private:
  enum class Phase{Release,Manifest,Package};
  void request(const QUrl&,Phase);
  void check();void download();void fail(const QString&);void stop();
  void install();
  void downloadTo(const QString&);
  friend struct UpdateDialogHarness;
  std::function<bool()> can_restart_;
  QPointer<update::Installer> installer_;
  QNetworkAccessManager* network_;
  QPointer<QNetworkReply> reply_;
  QTimer timeout_;
  QByteArray metadata_;
  update::Release release_;
  QSaveFile file_;
  QCryptographicHash hash_{QCryptographicHash::Sha256};
  qint64 downloaded_=0;
  QString path_;
  QLabel* status_;
  QPlainTextEdit* notes_;
  QProgressBar* progress_;
  QPushButton *check_,*download_,*open_,*cancel_;
};
}
