#include <QCoreApplication>
#include <QCryptographicHash>
#include <QImage>
#include <cstring>
#include <iostream>
int main(int argc,char** argv){
  QCoreApplication app(argc,argv);if(argc!=3)return 2;
  auto a=QImage(QString::fromLocal8Bit(argv[1])).convertToFormat(QImage::Format_Grayscale8);
  auto b=QImage(QString::fromLocal8Bit(argv[2])).convertToFormat(QImage::Format_Grayscale8);
  if(a.isNull()||b.isNull()||a.size()!=b.size())return 3;
  QCryptographicHash hash(QCryptographicHash::Sha256);
  for(int y=0;y<a.height();++y){
    if(std::memcmp(a.constScanLine(y),b.constScanLine(y),size_t(a.width())))return 4;
    hash.addData(QByteArrayView(reinterpret_cast<const char*>(a.constScanLine(y)),a.width()));
  }
  std::cout<<hash.result().toHex().toStdString()<<'\n';return 0;
}
