#include "update/release.hpp"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <iostream>
#include <stdexcept>
using namespace prism_viewer::update;
void check(bool v){if(!v)throw std::runtime_error("update test failed");}
template<class F>void reject(F f){bool yes=false;try{f();}catch(const std::exception&){yes=true;}check(yes);}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{
  for(const auto& target:QStringList{"windows-x64","macos-arm64","linux-x64","linux-arm64"}){
    QJsonObject asset{{"name","Prism-Viewer-1.3.0-"+target+(target.startsWith("linux-")?".tar.gz":".zip")},
      {"size",1234},{"sha256",QString(64,'a')}};
    QJsonObject o{{"schema",1},{"version","1.3.0"},{"revision",QString(40,'b')},{"build_epoch",500},
      {"platforms",QJsonObject{{target,asset}}}};
    const auto parse=[&]{return parseManifest(QJsonDocument(o).toJson(),target);};
    auto r=parse();check(r.size==1234&&r.sha256==QByteArray(32,char(0xaa))&&trustedUrl(r.url));
    check(newer(r,"1.2.1",900,QString(40,'c')));check(newer(r,"1.3.0",499,QString(40,'c')));
    check(!newer(r,"1.3.0",500,QString(40,'c'))&&!newer(r,"1.3.0",499,QString(40,'b')));
    check(!newer(r,"1.4.0",1,QString(40,'c')));
    for(auto bad:{QString("../evil.zip"),QString("/tmp/evil"),QString("wrong-platform.zip")}){
      auto a=asset;a["name"]=bad;o["platforms"]=QJsonObject{{target,a}};reject(parse);
    }
    for(auto n:{-1.,0.,1.5,2147483649.}){auto a=asset;a["size"]=n;o["platforms"]=QJsonObject{{target,a}};reject(parse);}
    auto a=asset;a["sha256"]="not-a-hash";o["platforms"]=QJsonObject{{target,a}};reject(parse);
  }
  for(auto url:{"http://github.com/DIBULI/Prism-Viewer/releases/download/v1.3.0/a.zip", "https://github.com.evil/a", "https://github.com/other/repo/releases/download/v1/a", "https://user@github.com/DIBULI/Prism-Viewer/releases/download/a", "file:///tmp/a"})check(!trustedUrl(QUrl(url)));
  QJsonObject asset{{"name","viewer-update.json"},{"size",100},{"state","uploaded"},
    {"browser_download_url","https://github.com/DIBULI/Prism-Viewer/releases/download/v1.3.0/viewer-update.json"}};
  QJsonObject release{{"draft",false},{"prerelease",false},{"tag_name","v1.3.0"},{"assets",QJsonArray{asset}}};
  auto manifest=[&]{return manifestUrl(QJsonDocument(release).toJson());};check(trustedUrl(manifest()));
  release["assets"]=QJsonArray{asset,asset};reject(manifest);release["assets"]=QJsonArray{asset};
  release["draft"]=true;reject(manifest);reject([]{parseManifest(QByteArray(1024*1024+1,' '),"linux-x64");});
  std::cout<<"PASS four platform assets, same-version revisions, no downgrade, checksum/size/schema/URL validation\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
