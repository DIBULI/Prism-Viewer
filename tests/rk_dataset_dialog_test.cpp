#include "ui/rk_dataset_dialog.hpp"
#include "ui/app_theme.hpp"
#include "common/ui_text.hpp"
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSettings>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QTableWidget>
#include <iostream>

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  prism_viewer::ui::applyLightApplicationTheme(app);
  QTemporaryDir settings;
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
  QTcpServer server;
  if (!server.listen(QHostAddress::LocalHost, 0)) return 1;
  QObject::connect(&server, &QTcpServer::newConnection, &app, [&] {
    auto* socket = server.nextPendingConnection();
    QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket] {
      auto request = socket->property("request").toByteArray() + socket->readAll();
      socket->setProperty("request", request);
      if (!request.contains("\r\n\r\n") || socket->property("answered").toBool()) return;
      socket->setProperty("answered", true);
      QJsonArray rows;
      for (int i=0; i<4; ++i) rows.append(QJsonObject{
        {"name", QStringLiteral("capture-19700101-011852-3541ff009a6%1").arg(i)},
        {"manifest", QJsonObject{{"complete", i != 2},
          {"viewer_format", i == 3 ? "" : "prism-dataset-v6"},
          {"frame_sets", 290}, {"lidar_points", 5441760}}}});
      const auto body = QJsonDocument(rows).toJson(QJsonDocument::Compact);
      socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size()) +
                    "\r\nConnection: close\r\n\r\n" + body);
      socket->disconnectFromHost();
    });
  });
  for (bool chinese : {false, true}) {
    prism_viewer::common::setChineseUi(chinese);
    bool passed = false;
    QTimer timeout, poll;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &app, [] {
      std::cerr << "FAIL dialog timeout\n"; std::exit(2);
    });
    QObject::connect(&poll, &QTimer::timeout, &app, [&] {
      auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
      if (!dialog) return;
      auto* table = dialog->findChild<QTableWidget*>("rkDatasetTable");
      auto* download = dialog->findChild<QPushButton*>("rkDatasetDownload");
      auto* formats = dialog->findChild<QComboBox*>("rkDatasetFormat");
      if (!table || table->rowCount() != 4 || !download->isEnabled()) return;
      passed = formats && formats->count() == 3 && table->currentRow() == 0;
      const QString prefix = qEnvironmentVariable("PRISM_RK_DIALOG_SCREENSHOT");
      if (!prefix.isEmpty()) passed = dialog->grab().save(prefix + (chinese ? "-zh.png" : "-en.png")) && passed;
      dialog->reject();
    });
    QTimer::singleShot(0, &app, [&] {
      auto* dialog = QApplication::activeModalWidget();
      if (!dialog) return;
      dialog->findChild<QLineEdit*>("rkDatasetAddress")->setText(
        QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
      dialog->findChild<QPushButton*>("rkDatasetRefresh")->click();
    });
    timeout.start(10000); poll.start(50);
    prism_viewer::ui::showRkDatasetDialog(nullptr, [](const QString&) {});
    if (!passed) return 3;
  }
  std::cout << "PASS RK download dialog: English/Chinese, asynchronous refresh, selection, three formats\n";
  return 0;
}
