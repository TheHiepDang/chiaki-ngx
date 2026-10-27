// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QDir>
#include <QtTest/QTest>
#include <memory>
class MockUpdater:public QObject {
    Q_OBJECT
    Q_PROPERTY(QString channel MEMBER channel NOTIFY changed)
    Q_PROPERTY(QString currentVersion MEMBER currentVersion NOTIFY changed)
    Q_PROPERTY(QString availableVersion MEMBER availableVersion NOTIFY changed)
    Q_PROPERTY(QString status MEMBER status NOTIFY changed)
    Q_PROPERTY(bool available MEMBER available NOTIFY changed)
    Q_PROPERTY(bool busy MEMBER busy NOTIFY changed)
    Q_PROPERTY(bool installing MEMBER installing NOTIFY changed)
    Q_PROPERTY(bool canInstall MEMBER canInstall NOTIFY changed)
    Q_PROPERTY(bool installAllowed MEMBER installAllowed NOTIFY changed)
    Q_PROPERTY(bool error MEMBER error NOTIFY changed)
    Q_PROPERTY(double progress MEMBER progress NOTIFY changed)
public:
    QString channel="release",currentVersion="0.1",availableVersion="0.2",status="Up to date";
    bool available=false,busy=false,installing=false,canInstall=true,installAllowed=true,error=false;
    double progress=0;int checks=0;
    Q_INVOKABLE void checkForUpdates(){++checks;}
signals:void changed();
};
class Backend:public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* updates READ service CONSTANT)
public:MockUpdater update;QObject *service(){return &update;}
};
#define CHECK(x) do {if(!(x)){qCritical()<<"FAIL"<<__LINE__<<#x;return 1;}}while(0)
int main(int argc,char **argv) {
    QGuiApplication app(argc,argv);auto font=app.font();font.setPixelSize(20);app.setFont(font);
    Backend backend;qmlRegisterSingletonInstance("org.streetpea.chiaking",1,0,"Chiaki",&backend);
    QQmlEngine engine;QQmlComponent component(&engine,QUrl::fromLocalFile(QStringLiteral(UPDATE_QML_PATH)));
    CHECK(!component.isError());std::unique_ptr<QObject> object(component.create());CHECK(object);
    auto root=qobject_cast<QQuickItem *>(object.get());CHECK(root);
    QQuickWindow window;window.resize(660,160);window.setColor(QColor("#252525"));QQmlComponent themeComponent(&engine);themeComponent.setData("import QtQuick; import QtQuick.Controls.Material; Item { Material.theme: Material.Dark; Material.accent: \"#00a7ff\" }",QUrl());
    std::unique_ptr<QObject> themeObject(themeComponent.create());auto theme=qobject_cast<QQuickItem *>(themeObject.get());CHECK(theme);theme->setParentItem(window.contentItem());theme->setSize(QSizeF(660,160));
    root->setParentItem(theme);root->setWidth(620);root->setHeight(150);window.show();window.requestActivate();QTest::qWait(50);
    auto channel=root->findChild<QQuickItem *>("updateChannel"),install=root->findChild<QQuickItem *>("installUpdate"),check=root->findChild<QQuickItem *>("checkUpdates");
    auto version=root->findChild<QObject *>("currentVersion");CHECK(channel && install && check && version);
    CHECK(version->property("text").toString()=="v0.1");CHECK(!install->isVisible() && check->isVisible());
    check->forceActiveFocus(Qt::TabFocusReason);QTest::keyClick(&window,Qt::Key_Return);CHECK(backend.update.checks==1);
    CHECK(QMetaObject::invokeMethod(channel,"activated",Q_ARG(int,1)));CHECK(backend.update.channel=="prerelease");
    backend.update.available=true;emit backend.update.changed();QTest::qWait(20);
    CHECK(install->isVisible() && !check->isVisible());CHECK(install->property("text").toString().contains("v0.2"));
    QSignalSpy requested(root,SIGNAL(installRequested()));install->forceActiveFocus(Qt::TabFocusReason);QTest::keyClick(&window,Qt::Key_Return);CHECK(requested.count()==1);
    backend.update.installAllowed=false;emit backend.update.changed();QTest::qWait(20);CHECK(!install->isEnabled());
    backend.update.installAllowed=true;backend.update.busy=true;backend.update.installing=true;backend.update.progress=.4;backend.update.status="Downloading";emit backend.update.changed();QTest::qWait(20);
    CHECK(!channel->isEnabled() && !install->isEnabled());
    auto status=root->findChild<QQuickItem *>("updateStatus");CHECK(status && status->isVisible() && status->property("text").toString().contains("40%"));
    backend.update.busy=false;backend.update.installing=false;backend.update.error=false;emit backend.update.changed();QTest::qWait(20);
    const QString previews=qEnvironmentVariable("CHIAKI_QML_PREVIEW_ROOT");
    if(!previews.isEmpty()){QDir().mkpath(previews);CHECK(window.grabWindow().save(previews+"/updates-available.png"));}
    window.resize(420,180);root->setWidth(390);QTest::qWait(30);
    auto position=version->property("text");Q_UNUSED(position);
    for(auto control:{channel,install}) {const auto p=control->mapToItem(root,QPointF(0,0));CHECK(p.x()>=0 && p.x()+control->width()<=root->width()+1);}
    if(!previews.isEmpty()) CHECK(window.grabWindow().save(previews+"/updates-compact.png"));
    backend.update.available=false;backend.update.error=true;backend.update.status="Could not check for updates. Retry when online.";emit backend.update.changed();QTest::qWait(20);CHECK(check->isVisible() && check->isEnabled() && status->isVisible());
    root->setParentItem(nullptr);
    qInfo()<<"PASS update footer: current version, channel, update prompt, keyboard confirmation, streaming/busy gates, progress and compact layout";
    return 0;
}
#include "update_ui.moc"
