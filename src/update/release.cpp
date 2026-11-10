#include "update/release.hpp"
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QVersionNumber>
#include <stdexcept>

namespace prism_viewer::update {
namespace {
void need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
QJsonObject object(const QByteArray& bytes){
  need(bytes.size()<=1024*1024,"Update metadata is too large");
  QJsonParseError error;auto doc=QJsonDocument::fromJson(bytes,&error);
  need(error.error==QJsonParseError::NoError&&doc.isObject(),"Invalid update metadata");return doc.object();
}
bool version(const QString& s){return QRegularExpression("^[0-9]+\\.[0-9]+\\.[0-9]+$").match(s).hasMatch();}
qint64 integer(const QJsonValue& v,qint64 limit){
  const auto n=v.toDouble(-1);need(n>=1&&n<=double(limit)&&double(qint64(n))==n,"Invalid update size or build number");return qint64(n);
}
QUrl assetUrl(const QString& revision,const QString& name){
  return QUrl("https://dibuli.com/updates/prism-viewer/"+revision+"/"+name);
}
}
QString platform(){
#if defined(Q_OS_WIN)
  return "windows-x64";
#elif defined(Q_OS_MACOS)
  return "macos-arm64";
#elif defined(__aarch64__)
  return "linux-arm64";
#else
  return "linux-x64";
#endif
}
bool trustedUrl(const QUrl& u){
  if(!u.isValid()||u.scheme()!="https"||u.host()!="dibuli.com"||!u.userInfo().isEmpty()||u.port(443)!=443||u.hasFragment()||u.hasQuery()||
      u.path().split('/').contains("..")||u.path().contains('\\'))return false;
  if(u.path()=="/updates/prism-viewer/viewer-update.json")return true;
  return QRegularExpression(
      "^/updates/prism-viewer/[0-9a-f]{40}/Prism-Viewer-[0-9]+\\.[0-9]+\\.[0-9]+-"
      "(?:(?:windows-x64|macos-arm64)\\.zip|linux-(?:x64|arm64)\\.tar\\.gz)$").match(u.path()).hasMatch();
}
QUrl manifestUrl(){
  return QUrl("https://dibuli.com/updates/prism-viewer/viewer-update.json");
}
Release parseManifest(const QByteArray& bytes,const QString& target){
  const auto o=object(bytes);need(o.value("schema").toInt()==1,"Unsupported update metadata version");
  Release r;r.version=o.value("version").toString();r.revision=o.value("revision").toString();
  need(version(r.version)&&QRegularExpression("^[0-9a-f]{40}$").match(r.revision).hasMatch(),"Invalid update identity");
  r.epoch=integer(o.value("build_epoch"),9007199254740991LL);r.notes=o.value("notes").toString().left(8000);
  need(QStringList{"windows-x64","macos-arm64","linux-x64","linux-arm64"}.contains(target),"Unsupported update platform");
  const auto a=o.value("platforms").toObject().value(target).toObject();
  r.name=a.value("name").toString();const auto suffix=target.startsWith("linux-")?".tar.gz":".zip";
  need(r.name=="Prism-Viewer-"+r.version+"-"+target+suffix,"Unexpected update package name");
  r.size=integer(a.value("size"),2LL*1024*1024*1024);
  const auto digest=a.value("sha256").toString();need(QRegularExpression("^[0-9a-f]{64}$").match(digest).hasMatch(),"Invalid update checksum");
  r.sha256=QByteArray::fromHex(digest.toLatin1());r.url=assetUrl(r.revision,r.name);return r;
}
bool newer(const Release& r,const QString& current,qint64 epoch,const QString& revision){
  need(version(current),"Invalid local version");
  const auto compare=QVersionNumber::compare(QVersionNumber::fromString(r.version),QVersionNumber::fromString(current));
  return compare>0||(compare==0&&r.epoch>epoch&&r.revision!=revision);
}
} // namespace prism_viewer::update
