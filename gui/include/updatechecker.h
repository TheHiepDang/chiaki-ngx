// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#pragma once
#include "releaseversion.h"
#include <QObject>
#include <QSettings>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QNetworkReply>
#include <QSaveFile>
#include <QCryptographicHash>
#include <QTimer>
#include <memory>

class UpdateChecker : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString channel READ channel WRITE setChannel NOTIFY changed)
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString availableVersion READ availableVersion NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool error READ error NOTIFY changed)
    Q_PROPERTY(bool installing READ installing NOTIFY changed)
    Q_PROPERTY(bool canInstall READ canInstall NOTIFY changed)
    Q_PROPERTY(bool installAllowed READ installAllowed NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
public:
    explicit UpdateChecker(QObject *parent=nullptr, QNetworkAccessManager *network=nullptr,
                           bool autoCheck=true, QString version=QString());
    QString channel() const {return channel_;}
    QString currentVersion() const {return version_.text;}
    QString availableVersion() const {return candidate_.version.text;}
    QString status() const {return status_;}
    bool available() const {return candidate_.version.valid && candidate_.version.compare(version_)>0;}
    bool busy() const {return phase_!=Idle;}
    bool error() const {return error_;}
    bool installing() const {return phase_==Downloading || phase_==Preparing;}
    bool canInstall() const;
    bool installAllowed() const {return installAllowed_;}
    double progress() const {return progress_;}
    void setInstallAllowed(bool value);
    void setChannel(const QString &value);
    Q_INVOKABLE void checkForUpdates();
    Q_INVOKABLE void installUpdate();
    Q_INVOKABLE void openReleasePage();
signals:
    void changed();
    void restartReady();
private:
    enum Phase {Idle,Checking,Downloading,Preparing};
    QSettings settings_;
    QNetworkAccessManager *network_;
    QPointer<QNetworkReply> reply_;
    QString channel_,status_,cacheDirectory_,archive_,stateFile_;
    NgxRelease::Version version_;
    NgxRelease::Candidate candidate_;
    QJsonArray releases_;
    QByteArray response_;
    std::unique_ptr<QSaveFile> file_;
    std::unique_ptr<QCryptographicHash> hash_;
    qint64 received_=0;
    quint64 generation_=0;
    Phase phase_=Idle;
    bool installAllowed_=true;
    bool error_=false;
    double progress_=0;
    QTimer installerPoll_;
    int pollCount_=0;
    void requestPage(int page,quint64 generation);
    bool drainDownload();
protected:
    // Platform capability seam lets download fixtures exercise every gate on any host.
    virtual bool supportsInstallation() const;
    virtual QString installerPath() const;
    virtual QString cacheRoot() const;
    virtual void prepareInstaller();
private:
    void fail(const QString &message);
    void cancelRequest();
};
