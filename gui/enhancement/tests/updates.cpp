// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "updatechecker.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QElapsedTimer>
#include <QtTest/QTest>
#include <QDebug>
#include <functional>
#define CHECK(x) do {if(!(x)){qCritical()<<"FAIL"<<__LINE__<<#x;return 1;}}while(0)

struct Response {QByteArray data;int status=200;int delay=0;};
class Reply final:public QNetworkReply {
    QByteArray data_;qint64 position_=0;bool done_=false;
public:
    Reply(const QNetworkRequest &request,const Response &response,QObject *parent):QNetworkReply(parent),data_(response.data) {
        setRequest(request);setUrl(request.url());setOperation(QNetworkAccessManager::GetOperation);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute,response.status);open(QIODevice::ReadOnly);
        if(response.status>=400) setError(QNetworkReply::ContentAccessDenied,"Fixture HTTP failure");
        QTimer::singleShot(response.delay,this,[this]{if(done_) return;emit readyRead();if(done_) return;done_=true;setFinished(true);emit finished();});
    }
    void abort() override {done_=true;setError(OperationCanceledError,"Canceled");setFinished(true);}
    qint64 bytesAvailable() const override {return data_.size()-position_+QNetworkReply::bytesAvailable();}
    bool isSequential() const override {return true;}
protected:
    qint64 readData(char *target,qint64 maximum) override {
        const qint64 count=qMin(maximum,qint64(data_.size())-position_);
        if(!count) return -1;
        memcpy(target,data_.constData()+position_,size_t(count));position_+=count;return count;
    }
};
class Network final:public QNetworkAccessManager {
public:
    QList<Response> responses;QList<QUrl> requests;
protected:
    QNetworkReply *createRequest(Operation,const QNetworkRequest &request,QIODevice *) override {
        requests.append(request.url());
        if(responses.isEmpty()) return new Reply(request,Response{"[]",500,0},this);
        return new Reply(request,responses.takeFirst(),this);
    }
};
class Checker final:public UpdateChecker {
    QString directory_,helper_;
public:
    bool prepared=false;
    Checker(Network *network,const QString &directory,bool autoCheck=false):UpdateChecker(nullptr,network,autoCheck,"0.1"),directory_(directory),helper_(directory+"/helper.ps1") {
        QDir().mkpath(directory);QFile file(helper_);file.open(QIODevice::WriteOnly);file.write("fixture");
    }
protected:
    QString installerPath() const override {return helper_;}
    QString cacheRoot() const override {return directory_;}
    void prepareInstaller() override {prepared=true;}
};
static bool until(const std::function<bool()> &condition,int limit=2500) {
    QElapsedTimer timer;timer.start();while(!condition() && timer.elapsed()<limit) QTest::qWait(5);return condition();
}
static QJsonObject release(const QString &version,bool prerelease=false,QByteArray bytes="fixture archive") {
    const QString tag="v"+version,name="chiaki-ngx-windows-x64-"+tag+".zip";
    QJsonObject asset{{"name",name},{"state","uploaded"},{"size",bytes.size()},
        {"digest","sha256:"+QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())},
        {"browser_download_url",QString::fromLatin1(NgxRelease::Repository)+"/releases/download/"+tag+"/"+name}};
    return {{"tag_name",tag},{"prerelease",prerelease},{"draft",false},{"assets",QJsonArray{asset}}};
}
static QByteArray body(const QJsonArray &array) {return QJsonDocument(array).toJson(QJsonDocument::Compact);}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);app.setOrganizationName("Chiaki-Update-Tests");app.setApplicationName("isolated");app.setApplicationVersion("0.1");
    QTemporaryDir temp;CHECK(temp.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
    QSettings settings;settings.clear();
    using NgxRelease::Version;
    CHECK(Version::parse("v0.1").compare(Version::parse("0.1.0"))==0);
    CHECK(Version::parse("0.10").compare(Version::parse("0.2"))>0);
    CHECK(Version::parse("0.2-rc.10").compare(Version::parse("0.2-rc.2"))>0);
    CHECK(Version::parse("0.2").compare(Version::parse("0.2-rc.99999999999999999999999"))>0);
    CHECK(Version::parse("0.2-alpha").compare(Version::parse("0.2-alpha.1"))<0);
    for(auto text:{"1","01.2","0.2-rc.01","65536.1","0.2\n","0.2/evil","0.2;bad"}) CHECK(!Version::parse(text).valid);
    auto legacy=QJsonObject{{"tag_name","pre-release"},{"name","chiaki-ngx-alpha-v0.1"},{"prerelease",false}};
    CHECK(NgxRelease::candidate(legacy).version.compare(Version::parse("0.1"))==0);
    auto disguised=release("0.3-rc.1",false);CHECK(NgxRelease::newest(QJsonArray{release("0.2"),disguised},false).version.text=="0.2");
    auto draft=release("9.0");draft["draft"]=true;
    auto evil=release("0.2");auto assets=evil["assets"].toArray();auto asset=assets[0].toObject();asset["browser_download_url"]="https://example.org/file.zip";assets[0]=asset;evil["assets"]=assets;
    CHECK(!NgxRelease::candidate(evil).installable());
    Network network;Checker checker(&network,temp.path()+"/downloads");
    const auto list=body(QJsonArray{release("0.3-rc.10",true),draft,legacy,release("0.2"),release("0.3-rc.2",true)});
    network.responses.append({list});checker.checkForUpdates();CHECK(until([&]{return !checker.busy();}));
    CHECK(!checker.error() && checker.available() && checker.availableVersion()=="0.2");
    network.responses.append({list});checker.setChannel("prerelease");CHECK(until([&]{return !checker.busy();}));
    CHECK(checker.availableVersion()=="0.3-rc.10" && settings.value("updates/channel").toString()=="prerelease");
    CHECK(network.requests[0].host()=="api.github.com");
    network.responses.append({"[]",403});checker.checkForUpdates();CHECK(until([&]{return !checker.busy();}));CHECK(checker.error());
    network.responses.append({body(QJsonArray{release("0.1")})});checker.checkForUpdates();CHECK(until([&]{return !checker.busy();}));CHECK(!checker.available() && !checker.error());
    network.responses.append({"not json"});checker.checkForUpdates();CHECK(until([&]{return !checker.busy();}));CHECK(checker.error());
    network.responses.append({QByteArray(4*1024*1024+1,'x')});checker.checkForUpdates();CHECK(until([&]{return !checker.busy();}));CHECK(checker.error());
    // Channel changes discard stale in-flight results.
    network.responses.append({body(QJsonArray{release("9.0")}),200,100});checker.checkForUpdates();
    network.responses.append({body(QJsonArray{release("0.2")})});checker.setChannel("release");CHECK(until([&]{return !checker.busy();}));QTest::qWait(120);CHECK(checker.availableVersion()=="0.2");
    // Automatic startup check is asynchronous and does not require a QML view.
    network.responses.append({body(QJsonArray{release("0.1")})});const auto before=network.requests.size();
    {Checker startup(&network,temp.path()+"/startup",true);CHECK(until([&]{return network.requests.size()>before && !startup.busy();}));}
    // Checksum mismatch fails before the installer is called.
    network.responses.append({body(QJsonArray{release("0.2",false,"expected bytes")})});checker.checkForUpdates();CHECK(until([&]{return !checker.busy();}));
    network.responses.append({"different data"});checker.installUpdate();CHECK(until([&]{return !checker.busy();}));CHECK(checker.error() && !checker.prepared);
    // Streaming blocks installation, without requesting a download.
    checker.setInstallAllowed(false);const auto count=network.requests.size();checker.installUpdate();CHECK(network.requests.size()==count && !checker.prepared);checker.setInstallAllowed(true);
    // Valid hash and byte count are both required. No real helper runs in this fixture.
    network.responses.append({body(QJsonArray{release("0.2",false,"expected bytes")})});checker.checkForUpdates();CHECK(until([&]{return !checker.busy();}));
    network.responses.append({"expected bytes"});checker.installUpdate();CHECK(until([&]{return checker.prepared;}));
    CHECK(!checker.error());
    // The production workflow uses three-component stable/RC tags and this exact asset name.
    auto canonical=NgxRelease::candidate(release("0.2.0"));
    CHECK(canonical.installable() && canonical.assetName=="chiaki-ngx-windows-x64-v0.2.0.zip");
    auto missingDigest=release("0.2.0");auto modernAssets=missingDigest["assets"].toArray();
    auto modernAsset=modernAssets[0].toObject();modernAsset.remove("digest");modernAssets[0]=modernAsset;missingDigest["assets"]=modernAssets;
    CHECK(!NgxRelease::candidate(missingDigest).installable());
    Network modernNetwork;Checker modernChecker(&modernNetwork,temp.path()+"/modern");
    const auto modernList=body(QJsonArray{release("0.3.0-rc.1",true),release("0.2.0")});
    modernNetwork.responses.append({modernList});modernChecker.checkForUpdates();CHECK(until([&]{return !modernChecker.busy();}));
    CHECK(modernChecker.availableVersion()=="0.2.0");
    CHECK(modernNetwork.requests[0].path()=="/repos/TheHiepDang/chiaki-ngx/releases");
    modernNetwork.responses.append({modernList});modernChecker.setChannel("prerelease");CHECK(until([&]{return !modernChecker.busy();}));
    CHECK(modernChecker.availableVersion()=="0.3.0-rc.1");
    qInfo()<<"PASS release ordering/channels/legacy baseline, canonical production tags/assets, startup checks, stale replies, errors, streaming gate and verified downloads";
    return 0;
}
