// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "updatechecker.h"
#include "../enhancement/diagnostics.h"
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QDateTime>
#include <QUuid>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

static QNetworkRequest updateRequest(const QUrl &url) {
    QNetworkRequest r(url);
    r.setRawHeader("User-Agent",("chiaki-ngx/"+QCoreApplication::applicationVersion()).toUtf8());
    r.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
    r.setTransferTimeout(20000);
    return r;
}
UpdateChecker::UpdateChecker(QObject *parent,QNetworkAccessManager *network,bool autoCheck,QString version)
    : QObject(parent),network_(network?network:new QNetworkAccessManager(this)) {
    version_=NgxRelease::Version::parse(version.isEmpty()?QCoreApplication::applicationVersion():version);
    channel_=settings_.value("updates/channel","release").toString();
    if(channel_!="release" && channel_!="prerelease") channel_="release";
    status_=tr("Updates checked at startup");
    qInfo()<<"[Update] current_version="<<version_.text<<"channel="<<channel_;
    const QString healthPath=qEnvironmentVariable("CHIAKI_UPDATE_READY_PATH");
    if(!healthPath.isEmpty()) QTimer::singleShot(0,this,[healthPath]{
        QSaveFile file(healthPath);
        if(file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(QJsonObject{{"state","started"},{"version",QCoreApplication::applicationVersion()}}).toJson());
            file.commit();
        }
    });
    installerPoll_.setInterval(100);
    connect(&installerPoll_,&QTimer::timeout,this,[this]{
        QFile state(stateFile_);
        if(state.open(QIODevice::ReadOnly)) {
            const auto result=QJsonDocument::fromJson(state.read(16384)).object();
            if(result.value("state").toString()=="ready") {
                if(result.value("version").toString()!=candidate_.version.text) {installerPoll_.stop();fail(tr("Update helper returned a different version."));return;}
                installerPoll_.stop();
                if(!installAllowed_) {fail(tr("A stream started; update canceled."));return;}
                status_=tr("Restarting to install v%1â€¦").arg(candidate_.version.text);emit changed();
                qInfo()<<"[Update] installer_ready=true restarting_for_version="<<candidate_.version.text;
                emit restartReady();
                QCoreApplication::quit();
            } else if(result.value("state").toString()=="error") {
                installerPoll_.stop();
                fail(tr("Could not prepare the update. See the update log."));
            }
        }
        if(++pollCount_>1200) {installerPoll_.stop();fail(tr("Update preparation timed out. See the update log."));}
    });
    if(autoCheck && !qEnvironmentVariableIsSet("CHIAKI_RENDER_DIAGNOSTIC"))
        QTimer::singleShot(1500,this,&UpdateChecker::checkForUpdates);
}
bool UpdateChecker::canInstall() const {
    return supportsInstallation() && available() && candidate_.installable();
}
bool UpdateChecker::supportsInstallation() const {
#if defined(Q_OS_WIN) && defined(Q_PROCESSOR_X86_64)
    return true;
#else
    return false;
#endif
}
void UpdateChecker::setInstallAllowed(bool value) {
    if(installAllowed_==value) return;
    installAllowed_=value;
    if(!value && installing()) {
        QFile cancel(cacheDirectory_+"/cancel");if(cancel.open(QIODevice::WriteOnly)) cancel.write("cancel");
        installerPoll_.stop();fail(tr("A stream started; update canceled."));
    }
    emit changed();
}
void UpdateChecker::cancelRequest() {
    ++generation_;
    if(reply_) {auto reply=reply_;reply_=nullptr;reply->disconnect(this);reply->abort();reply->deleteLater();}
    if(file_) {file_->cancelWriting();file_.reset();}
}
void UpdateChecker::setChannel(const QString &value) {
    if(installing() || value==channel_ || (value!="release" && value!="prerelease")) return;
    channel_=value;settings_.setValue("updates/channel",value);settings_.sync();
    qInfo()<<"[Update] selected_channel="<<value;
    checkForUpdates();
}
void UpdateChecker::fail(const QString &message) {
    cancelRequest();phase_=Idle;status_=message;progress_=0;error_=true;
    qWarning()<<"[Update]"<<message;
    emit changed();
}
void UpdateChecker::checkForUpdates() {
    if(installing()) return;
    cancelRequest();candidate_={};releases_={};phase_=Checking;error_=false;status_=tr("Checking for updatesâ€¦");emit changed();
    if(!version_.valid) {fail(tr("This build has no valid release version."));return;}
    requestPage(1,generation_);
}
void UpdateChecker::requestPage(int page,quint64 generation) {
    auto request=updateRequest(QUrl(QString::fromLatin1(NgxRelease::Api)+QString("?per_page=100&page=%1").arg(page)));
    request.setRawHeader("Accept","application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version","2022-11-28");
    reply_=network_->get(request);auto reply=reply_;response_.clear();reply->setReadBufferSize(256*1024);
    connect(reply,&QNetworkReply::readyRead,this,[this,reply,generation]{
        if(generation!=generation_ || reply_!=reply) return;
        response_+=reply->readAll();
        if(response_.size()>4*1024*1024) fail(tr("The release response was too large."));
    });
    connect(reply,&QNetworkReply::finished,this,[this,reply,generation,page]{
        if(generation!=generation_ || reply_!=reply) return;
        response_+=reply->readAll();reply_=nullptr;reply->deleteLater();
        const int http=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if(reply->error()!=QNetworkReply::NoError || http!=200) {
            qWarning()<<"[Update] release_check_failed http_status="<<http<<"network_error="<<int(reply->error());
            fail(http==403 || http==429?tr("GitHub limit reached; try again later."):tr("Could not check for updates. Retry when online."));return;
        }
        QJsonParseError error;auto doc=QJsonDocument::fromJson(response_,&error);
        if(response_.size()>4*1024*1024 || error.error!=QJsonParseError::NoError || !doc.isArray()) {fail(tr("GitHub returned an invalid release list."));return;}
        const auto entries=doc.array();for(const auto &entry:entries) releases_.append(entry);
        if(entries.size()==100) {
            if(page>=10) {fail(tr("Too many releases to check reliably."));return;}
            requestPage(page+1,generation);return;
        }
        candidate_=NgxRelease::newest(releases_,channel_=="prerelease");phase_=Idle;
        status_=available()?tr("v%1 is available").arg(candidate_.version.text):tr("Youâ€™re up to date");
        if(available() && !candidate_.installable()) status_=tr("v%1 is available; view its release page").arg(candidate_.version.text);
        qInfo()<<"[Update] check_complete channel="<<channel_<<"current="<<version_.text<<"latest="<<candidate_.version.text<<"available="<<available()<<"verified_windows_asset="<<candidate_.installable();
        emit changed();
    });
}
void UpdateChecker::openReleasePage() {QDesktopServices::openUrl(candidate_.page.isEmpty()?QUrl(QString::fromLatin1(NgxRelease::Repository)+"/releases"):candidate_.page);}
void UpdateChecker::installUpdate() {
    if(busy() || !available()) return;
    if(!installAllowed_) {status_=tr("End the stream before updating.");emit changed();return;}
    if(!canInstall()) {openReleasePage();return;}
    const QString helper=installerPath();
    if(!QFileInfo::exists(helper)) {fail(tr("Portable updater missing. Download the complete release ZIP."));return;}
    cacheDirectory_=cacheRoot()+"/"+QUuid::createUuid().toString(QUuid::WithoutBraces);
    if(!QDir().mkpath(cacheDirectory_)) {fail(tr("Could not create the update cache."));return;}
    error_=false;archive_=cacheDirectory_+"/package.zip";file_=std::make_unique<QSaveFile>(archive_);
    if(!file_->open(QIODevice::WriteOnly)) {fail(tr("Could not save the update."));return;}
    hash_=std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);received_=0;progress_=0;phase_=Downloading;
    status_=tr("Downloading v%1â€¦").arg(candidate_.version.text);emit changed();
    const auto generation=generation_;reply_=network_->get(updateRequest(candidate_.download));auto reply=reply_;reply->setReadBufferSize(256*1024);
    qInfo()<<"[Update] download_started version="<<candidate_.version.text<<"expected_bytes="<<candidate_.size;
    connect(reply,&QNetworkReply::readyRead,this,[this,reply,generation]{if(generation==generation_ && reply_==reply) drainDownload();});
    connect(reply,&QNetworkReply::finished,this,[this,reply,generation]{
        if(generation!=generation_ || reply_!=reply) return;
        if(!drainDownload()) return;
        reply_=nullptr;reply->deleteLater();
        if(reply->error()!=QNetworkReply::NoError || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()!=200) {fail(tr("Update download failed. Your installation is unchanged."));return;}
        if(received_!=candidate_.size || QString::fromLatin1(hash_->result().toHex())!=candidate_.sha256) {fail(tr("Update checksum failed. Your installation is unchanged."));return;}
        if(!file_->commit()) {fail(tr("Could not finish saving the update."));return;}
        file_.reset();hash_.reset();qInfo()<<"[Update] checksum_verified version="<<candidate_.version.text;
        prepareInstaller();
    });
}
bool UpdateChecker::drainDownload() {
    if(!reply_ || !file_) return false;
    const auto data=reply_->readAll();received_+=data.size();
    if(received_>candidate_.size || received_>1024LL*1024*1024) {fail(tr("Update exceeded its expected size."));return false;}
    if(file_->write(data)!=data.size()) {fail(tr("Could not write the update. Check free disk space."));return false;}
    hash_->addData(data);progress_=double(received_)/candidate_.size;emit changed();return true;
}
QString UpdateChecker::installerPath() const {return QCoreApplication::applicationDirPath()+"/updates/Install-Update.ps1";}
QString UpdateChecker::cacheRoot() const {return QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/updates";}
void UpdateChecker::prepareInstaller() {
#ifdef Q_OS_WIN
    const QString helper=cacheDirectory_+"/Install-Update.ps1";
    if(!QFile::copy(installerPath(),helper)) {fail(tr("Could not prepare the update helper."));return;}
    const QString windows=qEnvironmentVariable("SystemRoot",QStringLiteral("C:/Windows"));
    const QString program=windows+"/System32/WindowsPowerShell/v1.0/powershell.exe";
    stateFile_=cacheDirectory_+"/state.json";
    const QString log=QFileInfo(DiagnosticLog::filename()).absolutePath()+"/chiaki_update_"+QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss_zzz")+".log";
    QProcess process;process.setProgram(program);
    process.setArguments({"-NoProfile","-NonInteractive","-ExecutionPolicy","Bypass","-WindowStyle","Hidden","-File",helper,
        "-ArchivePath",QDir::toNativeSeparators(archive_),"-InstallDirectory",QDir::toNativeSeparators(QCoreApplication::applicationDirPath()),
        "-ParentPid",QString::number(QCoreApplication::applicationPid()),"-ExpectedVersion",candidate_.version.text,
        "-ExpectedSha256",candidate_.sha256,"-StatePath",QDir::toNativeSeparators(stateFile_),"-LogPath",QDir::toNativeSeparators(log)});
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a){a->flags|=CREATE_NO_WINDOW;});
    if(!process.startDetached()) {fail(tr("Could not start the update helper."));return;}
    phase_=Preparing;status_=tr("Preparing updateâ€¦");progress_=1;pollCount_=0;installerPoll_.start();
    qInfo()<<"[Update] preparing_version="<<candidate_.version.text<<"installer_log="<<log;emit changed();
#else
    fail(tr("Automatic installation is available for Windows portable builds."));
#endif
}
