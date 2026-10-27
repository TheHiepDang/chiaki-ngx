// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlPropertyMap>
#include <QDebug>
#include <memory>
#include <QFileInfo>
#include <QDir>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <QEventLoop>
#include <QtTest/QTest>
#include <QSignalSpy>

class NrSettingsMap : public QQmlPropertyMap
{
    Q_OBJECT
public:
    NrSettingsMap() : QQmlPropertyMap(this, nullptr) {}
    Q_INVOKABLE void queueEnhancementPercent(int value) {
        insert("enhancementPercentPreview", value);
        insert("enhancementResolutionPending", true);
    }
};

class Backend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QQmlPropertyMap *settings READ settings CONSTANT)
    Q_PROPERTY(QQmlPropertyMap *window READ window CONSTANT)
    NrSettingsMap settingsMap;
    QQmlPropertyMap windowMap;

  public:
    Backend()
    {
        settingsMap.insert("enhancementNeural", false);
        settingsMap.insert("enhancementCustomizePasses", false);
        settingsMap.insert("enhancementNrPreset", 0);
        settingsMap.insert("enhancementIntensity", 1.0);
        settingsMap.insert("enhancementLocalStructure", 1.0);
        settingsMap.insert("enhancementLocalTone", 1.0);
        settingsMap.insert("enhancementSkinStructure", -1.0);
        settingsMap.insert("enhancementAutomaticMask", false);
        settingsMap.insert("enhancementPassCount", 1);
        settingsMap.insert("enhancementEditingPass", 0);
        settingsMap.insert("enhancementResolutionPending", false);
        settingsMap.insert("eyeCandyExposure", 0);
        settingsMap.insert("eyeCandyDarkArea", 0);
        settingsMap.insert("showStreamStats", false);
        settingsMap.insert("enhancementPercent", 65);
        settingsMap.insert("enhancementPercentPreview", 65);
        settingsMap.insert("placeboColorAdjustmentEnabled", true);
        settingsMap.insert("placeboColorAdjustmentPreset", 0);
        settingsMap.insert("placeboColorAdjustmentBrightness", 0.0);
        settingsMap.insert("placeboColorAdjustmentContrast", 1.0);
        settingsMap.insert("placeboColorAdjustmentSaturation", 1.0);
        settingsMap.insert("placeboColorAdjustmentGamma", 1.0);
        windowMap.insert("rtxSettingsSupported", true);
        settingsMap.insert("rendererShortcut", "Ctrl+Shift+F");
        windowMap.insert("enhancementApplying", false);
        settingsMap.insert("enhancementFsr", true);
        settingsMap.insert("enhancementSharpness", 20);
        settingsMap.insert("enhancementFrameGeneration", false);
        windowMap.insert("enhancementUpscalingEligible", false);
        windowMap.insert("enhancementNeuralAvailable", true);
        windowMap.insert("enhancementCadence", "No enhanced frames presented");
        windowMap.insert("enhancementDimensions", "Native 1920×1080 · NR 1248×702");
        windowMap.insert("enhancementStatus", "Off");
    }
    QQmlPropertyMap *settings()
    {
        return &settingsMap;
    }
    QQmlPropertyMap *window()
    {
        return &windowMap;
    }
};
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            qCritical() << "FAIL line" << __LINE__ << #x;                                          \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    auto uiFont = app.font();
    uiFont.setPixelSize(20);
    app.setFont(uiFont);
    Backend backend;
    qmlRegisterSingletonInstance("org.streetpea.chiaking", 1, 0, "Chiaki", &backend);
    QQmlEngine engine;
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(ENHANCEMENT_QML)));
    if (component.isError())
    {
        qCritical() << component.errors();
        return 1;
    }
    std::unique_ptr<QObject> root(component.create());
    CHECK(root);
    auto nr = root->findChild<QObject *>("nrToggle"), fsr = root->findChild<QObject *>("fsrToggle"),
         sharp = root->findChild<QObject *>("sharpness"),
         fg = root->findChild<QObject *>("fgToggle"), scale = root->findChild<QObject *>("nrScale");
    CHECK(nr && fsr && sharp && fg && scale);
    auto passCount = root->findChild<QObject *>("nrPassCount");
    auto editingPass = root->findChild<QObject *>("nrEditingPass");
    CHECK(passCount && editingPass);
    CHECK(passCount->property("currentIndex").toInt() == 0);
    CHECK(!editingPass->property("visible").toBool());
    auto stats = root->findChild<QObject *>("gameplayStatsToggle");
    CHECK(stats && !stats->property("checked").toBool());
    backend.settings()->insert("showStreamStats", true);
    app.processEvents();
    CHECK(stats->property("checked").toBool());
    CHECK(!nr->property("checked").toBool());
    CHECK(!fg->property("checked").toBool());
    CHECK(!fsr->property("enabled").toBool());
    CHECK(!sharp->property("enabled").toBool());
    CHECK(fg->property("enabled").toBool());
    backend.settings()->insert("enhancementNeural", true);
    app.processEvents();
    CHECK(scale->property("enabled").toBool());
    CHECK(passCount->property("enabled").toBool());
    CHECK(QMetaObject::invokeMethod(passCount, "activated", Q_ARG(int, 2)));
    CHECK(backend.settings()->value("enhancementPassCount").toInt() == 3);
    app.processEvents();
    CHECK(!editingPass->property("visible").toBool());
    auto customize = root->findChild<QObject *>("nrCustomizePasses");
    auto preset = root->findChild<QObject *>("nrPreset");
    auto structure = root->findChild<QObject *>("nrLocalStructure");
    auto tone = root->findChild<QObject *>("nrLocalTone");
    auto skin = root->findChild<QObject *>("nrSkinStructure");
    auto mask = root->findChild<QObject *>("nrAutomaticMask");
    CHECK(customize && preset && structure && tone && skin && mask);
    CHECK(customize->property("visible").toBool());
    CHECK(customize->setProperty("checked", true));
    CHECK(QMetaObject::invokeMethod(customize, "toggled"));
    CHECK(backend.settings()->value("enhancementCustomizePasses").toBool());
    app.processEvents();
    CHECK(editingPass->property("visible").toBool());
    CHECK(editingPass->property("count").toInt() == 3);
    CHECK(preset->property("count").toInt() == 3);
    CHECK(QMetaObject::invokeMethod(preset, "activated", Q_ARG(int, 2)));
    CHECK(backend.settings()->value("enhancementNrPreset").toInt() == 2);
    CHECK(backend.settings()->value("enhancementLocalTone").toDouble() == 1.0);
    CHECK(!skin->property("enabled").toBool());
    CHECK(mask->setProperty("checked", true));
    CHECK(QMetaObject::invokeMethod(mask, "toggled"));
    app.processEvents();
    CHECK(skin->property("enabled").toBool());
    CHECK(structure->setProperty("value", 0.75));
    CHECK(QMetaObject::invokeMethod(structure, "moved"));
    CHECK(backend.settings()->value("enhancementLocalStructure").toDouble() == 0.75);
    CHECK(tone->setProperty("value", 0.5));
    CHECK(QMetaObject::invokeMethod(tone, "moved"));
    CHECK(backend.settings()->value("enhancementLocalTone").toDouble() == 0.5);
    CHECK(skin->setProperty("value", 1.25));
    CHECK(QMetaObject::invokeMethod(skin, "moved"));
    CHECK(backend.settings()->value("enhancementSkinStructure").toDouble() == 1.25);
    auto intensity = root->findChild<QObject *>("nrIntensity");
    CHECK(intensity && intensity->property("from").toDouble() == 0 && intensity->property("to").toDouble() == 1);
    intensity->setProperty("value", 0.45);
    CHECK(QMetaObject::invokeMethod(intensity, "moved"));
    CHECK(backend.settings()->value("enhancementIntensity").toDouble() == 0.45);
    // A reset changes only its setting; resolution queues rather than committing.
    backend.settings()->insert("enhancementPercent", 77);
    backend.settings()->insert("enhancementPercentPreview", 77);
    backend.settings()->insert("enhancementSharpness", 83);
    const QStringList controls = {"Intensity", "LocalStructure", "LocalTone", "SkinStructure", "Sharpness", "Resolution"};
    const QStringList properties = {"enhancementIntensity", "enhancementLocalStructure", "enhancementLocalTone", "enhancementSkinStructure", "enhancementSharpness", "enhancementPercentPreview"};
    const QList<double> defaults = {1, 1, 1, -1, 20, 65};
    backend.window()->insert("enhancementUpscalingEligible", true);
    app.processEvents();
    for (int n=0;n<controls.size();++n) {
        auto reset = root->findChild<QObject *>(("resetNr" + controls[n]).toUtf8().constData());
        CHECK(reset && reset->property("text").toString() == QString::fromUtf8("\xe2\x86\xba"));
        CHECK(reset->property("enabled").toBool());
        QList<QVariant> before;
        for (const auto &property:properties) before.append(backend.settings()->value(property));
        CHECK(QMetaObject::invokeMethod(reset, "clicked"));
        CHECK(backend.settings()->value(properties[n]).toDouble() == defaults[n]);
        for(int k=0;k<properties.size();++k) if(k!=n) CHECK(backend.settings()->value(properties[k])==before[k]);
    }
    CHECK(backend.settings()->value("enhancementPercent").toInt()==77);
    CHECK(backend.settings()->value("enhancementResolutionPending").toBool());
    backend.settings()->insert("enhancementPercent", 65);
    backend.settings()->insert("enhancementResolutionPending", false);
    backend.window()->insert("enhancementUpscalingEligible", false);
    CHECK(QMetaObject::invokeMethod(editingPass, "activated", Q_ARG(int, 1)));
    CHECK(backend.settings()->value("enhancementEditingPass").toInt() == 1);
    backend.window()->insert("enhancementApplying", true);
    app.processEvents();
    CHECK(!passCount->property("enabled").toBool());
    CHECK(!customize->property("enabled").toBool() && !preset->property("enabled").toBool());
    CHECK(!intensity->property("enabled").toBool());
    for (const auto &control:controls) CHECK(!root->findChild<QObject *>(("resetNr"+control).toUtf8().constData())->property("enabled").toBool());
    CHECK(!structure->property("enabled").toBool() && !tone->property("enabled").toBool() && !skin->property("enabled").toBool());
    CHECK(!editingPass->property("enabled").toBool());
    backend.window()->insert("enhancementApplying", false);
    backend.settings()->insert("enhancementPassCount", 1);
    backend.settings()->insert("enhancementEditingPass", 0);
    app.processEvents();
    CHECK(!editingPass->property("visible").toBool());
    CHECK(!fsr->property("enabled").toBool());
    backend.window()->insert("enhancementUpscalingEligible", true);
    app.processEvents();
    CHECK(fsr->property("enabled").toBool());
    CHECK(sharp->property("enabled").toBool());
    backend.window()->insert("enhancementUpscalingEligible", false);
    app.processEvents();
    CHECK(!fsr->property("enabled").toBool());
    CHECK(fsr->property("checked").toBool());
    backend.settings()->insert("enhancementPercent", 100);
    app.processEvents();
    CHECK(!root->property("showUpscaler").toBool());
    CHECK(!fsr->property("visible").toBool());
    CHECK(!root->property("showSharpness").toBool());
    CHECK(fsr->property("checked").toBool());
    CHECK(backend.settings()->value("enhancementSharpness").toInt() == 20);
    backend.settings()->insert("enhancementPercent", 65);
    app.processEvents();
    CHECK(root->property("showUpscaler").toBool());
    CHECK(fsr->property("visible").toBool());
    CHECK(root->property("showSharpness").toBool());
    backend.settings()->insert("enhancementFsr", false);
    app.processEvents();
    CHECK(!root->property("showSharpness").toBool());
    CHECK(backend.settings()->value("enhancementSharpness").toInt() == 20);
    backend.settings()->insert("enhancementFsr", true);
    app.processEvents();
    CHECK(root->property("showSharpness").toBool());
    backend.settings()->insert("enhancementNeural", false);
    app.processEvents();
    CHECK(fg->property("enabled").toBool());
    backend.settings()->insert("enhancementNeural", true);
    backend.window()->insert("enhancementNeuralAvailable", false);
    app.processEvents();
    CHECK(!nr->property("enabled").toBool());
    CHECK(!nr->property("checked").toBool());
    CHECK(backend.settings()->value("enhancementNeural").toBool());
    backend.window()->insert("enhancementNeuralAvailable", true);
    app.processEvents();
    CHECK(nr->property("checked").toBool());
    QQmlComponent liveComponent(&engine, QUrl::fromLocalFile(
        QFileInfo(QStringLiteral(ENHANCEMENT_QML)).dir().filePath("LiveRendererDialog.qml")));
    std::unique_ptr<QObject> live(liveComponent.create());
    if (!live) qCritical() << liveComponent.errors();
    CHECK(live);
    CHECK(live->findChild<QObject *>("nrToggle"));
    CHECK(live->findChild<QObject *>("rendererTab")->property("text").toString() == "RTX settings");
    QQmlComponent shortcutComponent(&engine, QUrl::fromLocalFile(
        QFileInfo(QStringLiteral(ENHANCEMENT_QML)).dir().filePath("RtxSettingsShortcut.qml")));
    std::unique_ptr<QObject> shortcut(shortcutComponent.create());
    if (!shortcut) qCritical() << shortcutComponent.errors();
    CHECK(shortcut);
    auto shortcutButton = shortcut->findChild<QObject *>("rtxSettingsShortcutButton");
    CHECK(shortcutButton && shortcutButton->property("text").toString() == "Ctrl+Shift+F");
    CHECK(shortcut->findChild<QObject *>("rtxSettingsShortcutLabel")->property("text").toString().startsWith("RTX settings"));
    QSignalSpy captureRequested(shortcut.get(), SIGNAL(captureRequested()));
    CHECK(QMetaObject::invokeMethod(shortcutButton, "clicked"));
    CHECK(captureRequested.count() == 1);
    auto tabs = live->findChild<QObject *>("rendererTabs");
    CHECK(tabs && tabs->property("count").toInt() == 2);
    tabs->setProperty("currentIndex", 1);
    app.processEvents();
    auto brightness = live->findChild<QObject *>("eyeCandyBrightness");
    auto contrast = live->findChild<QObject *>("eyeCandyContrast");
    auto saturation = live->findChild<QObject *>("eyeCandySaturation");
    auto gamma = live->findChild<QObject *>("eyeCandyGamma");
    CHECK(brightness && contrast && saturation && gamma);
    backend.settings()->insert("placeboColorAdjustmentBrightness", 0.10);
    app.processEvents();
    CHECK(qAbs(brightness->property("value").toDouble() - 0.10) < 0.001);
    brightness->setProperty("value", 0.12);
    CHECK(QMetaObject::invokeMethod(brightness, "moved"));
    CHECK(qAbs(backend.settings()->value("placeboColorAdjustmentBrightness").toDouble() - 0.12) < 0.001);
    CHECK(qAbs(brightness->property("from").toDouble() + 0.14) < 0.001);
    CHECK(qAbs(brightness->property("to").toDouble() - 0.14) < 0.001);
    CHECK(qAbs(contrast->property("to").toDouble() - 14.3) < 0.001);
    CHECK(qAbs(saturation->property("to").toDouble() - 14.3) < 0.001);
    CHECK(qAbs(gamma->property("to").toDouble() - 14.3) < 0.001);
    // Reset each shared setting independently, including old out-of-range values.
    backend.settings()->insert("placeboColorAdjustmentBrightness", 0.5);
    backend.settings()->insert("placeboColorAdjustmentContrast", 20.0);
    backend.settings()->insert("placeboColorAdjustmentSaturation", 21.0);
    backend.settings()->insert("placeboColorAdjustmentGamma", 22.0);
    app.processEvents();
    CHECK(backend.settings()->value("placeboColorAdjustmentContrast").toDouble() == 20.0);
    const QStringList names = {"Brightness", "Contrast", "Saturation", "Gamma"};
    for (int n = 0; n < names.size(); ++n) {
        auto reset = live->findChild<QObject *>(("resetEyeCandy" + names[n]).toUtf8().constData());
        CHECK(reset);
        CHECK(QMetaObject::invokeMethod(reset, "clicked"));
        CHECK(backend.settings()->value("placeboColorAdjustment" + names[n]).toDouble() == (n == 0 ? 0.0 : 1.0));
        for (int later = n + 1; later < names.size(); ++later)
            CHECK(backend.settings()->value("placeboColorAdjustment" + names[later]).toDouble() == 19.0 + later);
    }
    for (const QString name : {QString("Exposure"), QString("DarkArea")}) {
        auto slider = live->findChild<QObject *>(("eyeCandy" + name).toUtf8().constData());
        auto reset = live->findChild<QObject *>(("resetEyeCandy" + name).toUtf8().constData());
        CHECK(slider && reset);
        slider->setProperty("value", 35);
        CHECK(QMetaObject::invokeMethod(slider, "moved"));
        CHECK(backend.settings()->value("eyeCandy" + name).toInt() == 35);
        CHECK(QMetaObject::invokeMethod(reset, "clicked"));
        CHECK(backend.settings()->value("eyeCandy" + name).toInt() == 0);
    }
    for (auto child : live->findChildren<QObject *>())
        CHECK(child->property("text").toString() != "Reset these four controls");
    auto background = live->findChild<QObject *>("rendererDialogBackground");
    CHECK(background);
    CHECK(qAbs(background->property("color").value<QColor>().alphaF() - 0.5) < 0.01);
    backend.settings()->insert("placeboColorAdjustmentEnabled", false);
    app.processEvents();
    CHECK(!contrast->property("enabled").toBool());
    tabs->setProperty("currentIndex", 0);
    backend.window()->insert("enhancementApplying", true);
    app.processEvents();
    CHECK(!nr->property("enabled").toBool());
    CHECK(!scale->property("enabled").toBool());
    CHECK(!fg->property("enabled").toBool());
    backend.window()->insert("enhancementApplying", false);
    app.processEvents();
    CHECK(nr->property("enabled").toBool());
    CHECK(scale->property("enabled").toBool());
    const QString previewRoot = qEnvironmentVariable("CHIAKI_QML_PREVIEW_ROOT");
    QQuickWindow window;
    window.resize(540, 1080);
    window.setColor(QColor("#406080"));
    auto item = qobject_cast<QQuickItem *>(live.get());
    CHECK(item);
    QQmlComponent wrapperComponent(&engine);
    wrapperComponent.setData("import QtQuick.Controls; Pane { font.pixelSize: 20; padding: 0; background: null }", QUrl());
    std::unique_ptr<QObject> wrapper(wrapperComponent.create());
    auto wrapperItem = qobject_cast<QQuickItem *>(wrapper.get());
    CHECK(wrapperItem);
    wrapperItem->setParentItem(window.contentItem());
    wrapperItem->setSize(QSizeF(540, 1080));
    item->setParentItem(qvariant_cast<QQuickItem *>(wrapper->property("contentItem")));
    item->setWidth(540);
    auto fitDialog = [&]() { item->setHeight(qMin(qreal(window.height()), item->implicitHeight())); };
    QObject::connect(item, &QQuickItem::implicitHeightChanged, &window, fitDialog);
    QObject::connect(&window, &QQuickWindow::heightChanged, &window, fitDialog);
    fitDialog();
    window.show();
    auto shortcutItem = qobject_cast<QQuickItem *>(shortcut.get());
    CHECK(shortcutItem);
    shortcutItem->setParentItem(window.contentItem());
    app.processEvents();
    CHECK(live->property("visible").toBool() && shortcut->property("visible").toBool());
    backend.window()->insert("rtxSettingsSupported", false);
    app.processEvents();
    CHECK(!live->property("visible").toBool() && !live->property("enabled").toBool());
    CHECK(!shortcut->property("visible").toBool() && !shortcut->property("enabled").toBool());
    CHECK(!shortcutButton->property("enabled").toBool());
    CHECK(backend.settings()->value("rendererShortcut").toString() == "Ctrl+Shift+F");
    backend.window()->insert("rtxSettingsSupported", true);
    app.processEvents();
    CHECK(live->property("visible").toBool() && live->property("enabled").toBool());
    CHECK(shortcut->property("visible").toBool() && shortcut->property("enabled").toBool());

    shortcutItem->setParentItem(nullptr);

    window.requestActivate();
    backend.settings()->insert("placeboColorAdjustmentEnabled", true);
    CHECK(QMetaObject::invokeMethod(live.get(), "focusInitialItem"));
    app.processEvents();
    auto rendererTab = live->findChild<QQuickItem *>("rendererTab");
    auto eyeTab = live->findChild<QQuickItem *>("eyeCandyTab");
    CHECK(rendererTab && eyeTab && rendererTab->hasActiveFocus());
    // Both tabs grow to their content, while a short window bounds the dialog and scrolls.
    auto rendererView = live->findChild<QQuickItem *>("rendererSettingsView");
    auto eyeView = live->findChild<QQuickItem *>("eyeCandySettingsView");
    CHECK(rendererView && eyeView);
    QTest::qWait(30);
    const qreal rendererHeight = item->height();
    CHECK(rendererHeight > 200 && rendererHeight < 1080);
    CHECK(rendererView->property("contentHeight").toDouble() <= rendererView->height() + 1);
    for (const char *name : {"rendererDimensions", "rendererStatus", "rendererActive"}) {
        auto footer = live->findChild<QQuickItem *>(name);
        CHECK(footer && !footer->isVisible());
    }
    tabs->setProperty("currentIndex", 1);
    QTest::qWait(30);
    CHECK(item->height() > 200 && item->height() < 1080);
    CHECK(qAbs(item->height() - item->implicitHeight()) <= 1);
    CHECK(eyeView->property("contentHeight").toDouble() <= eyeView->height() + 1);
    window.resize(540, 250);
    wrapperItem->setHeight(250);
    QTest::qWait(30);
    CHECK(item->height() <= 250);
    CHECK(eyeView->property("contentHeight").toDouble() > eyeView->height());
    auto lastReset = live->findChild<QQuickItem *>("resetEyeCandyDarkArea");
    CHECK(lastReset);
    lastReset->forceActiveFocus(Qt::TabFocusReason);
    QTest::qWait(30);
    const auto lastPosition = lastReset->mapToItem(item, QPointF(0, 0));
    CHECK(lastPosition.y() >= 0 && lastPosition.y() + lastReset->height() <= item->height() + 1);
    window.resize(540, 1080);
    wrapperItem->setHeight(1080);
    tabs->setProperty("currentIndex", 0);
    CHECK(QMetaObject::invokeMethod(live.get(), "focusInitialItem"));
    QTest::qWait(30);
    QTest::keyClick(&window, Qt::Key_Right);
    CHECK(eyeTab->hasActiveFocus() && tabs->property("currentIndex").toInt() == 1);
    QTest::keyClick(&window, Qt::Key_Left);
    CHECK(rendererTab->hasActiveFocus() && tabs->property("currentIndex").toInt() == 0);
    QTest::keyClick(&window, Qt::Key_Tab);
    CHECK(eyeTab->hasActiveFocus() && tabs->property("currentIndex").toInt() == 1);
    QTest::keyClick(&window, Qt::Key_Backtab, Qt::ShiftModifier);
    CHECK(rendererTab->hasActiveFocus() && tabs->property("currentIndex").toInt() == 0);
    // These are exactly the PageUp/PageDown keys sent by the existing L1/R1 mapping.
    QTest::keyClick(&window, Qt::Key_PageDown);
    CHECK(eyeTab->hasActiveFocus() && tabs->property("currentIndex").toInt() == 1);
    QTest::keyClick(&window, Qt::Key_Down);
    auto eyeEnabled = live->findChild<QQuickItem *>("eyeCandyEnabled");
    CHECK(eyeEnabled && eyeEnabled->hasActiveFocus());
    QTest::keyClick(&window, Qt::Key_PageUp);
    CHECK(rendererTab->hasActiveFocus() && tabs->property("currentIndex").toInt() == 0);
    QTest::keyClick(&window, Qt::Key_Tab, Qt::ControlModifier);
    CHECK(eyeTab->hasActiveFocus() && tabs->property("currentIndex").toInt() == 1);
    auto exposure = live->findChild<QQuickItem *>("eyeCandyExposure");
    CHECK(exposure);
    live->setProperty("restoreFocusItem", QVariant::fromValue(exposure));
    CHECK(QMetaObject::invokeMethod(live.get(), "showEnhancements"));
    CHECK(tabs->property("currentIndex").toInt() == 1 && exposure->isVisible());
    live->setProperty("restoreFocusItem", QVariant::fromValue<QQuickItem *>(nullptr));
    exposure->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(&window, Qt::Key_Right);
    CHECK(backend.settings()->value("eyeCandyExposure").toInt() == 5);
    QTest::keyClick(&window, Qt::Key_Down);
    auto exposureReset = live->findChild<QQuickItem *>("resetEyeCandyExposure");
    CHECK(exposureReset && exposureReset->hasActiveFocus());
    QTest::keyClick(&window, Qt::Key_Return);
    CHECK(backend.settings()->value("eyeCandyExposure").toInt() == 0);
    auto darkArea = live->findChild<QQuickItem *>("eyeCandyDarkArea");
    CHECK(darkArea);
    darkArea->forceActiveFocus(Qt::TabFocusReason);
    app.processEvents();
    const auto darkPosition = darkArea->mapToItem(item, QPointF(0, 0));
    CHECK(darkPosition.y() >= 0 && darkPosition.y() + darkArea->height() <= item->height());
    auto darkReset = live->findChild<QQuickItem *>("resetEyeCandyDarkArea");
    CHECK(darkReset);
    darkReset->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(&window, Qt::Key_Down);
    CHECK(rendererTab->hasActiveFocus() && tabs->property("currentIndex").toInt() == 0);
    if (!previewRoot.isEmpty()) {
        QDir().mkpath(previewRoot);
        auto capture = [&](const QString &name) {
            QEventLoop loop;
            QTimer::singleShot(400, &loop, &QEventLoop::quit);
            loop.exec();
            return window.grabWindow().save(previewRoot + "/" + name + ".png");
        };
        backend.settings()->insert("enhancementCustomizePasses", false);
        backend.settings()->insert("enhancementPassCount", 3);
        backend.window()->insert("enhancementUpscalingEligible", true);
        CHECK(capture("renderer"));
        backend.settings()->insert("enhancementCustomizePasses", true);
        backend.settings()->insert("enhancementPassCount", 3);
        backend.settings()->insert("enhancementEditingPass", 1);
        app.processEvents();
        auto intensityReset = live->findChild<QQuickItem *>("resetNrIntensity");
        CHECK(intensityReset);
        backend.settings()->insert("enhancementIntensity", 0.3);
        intensityReset->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(&window, Qt::Key_Return);
        app.processEvents();
        CHECK(backend.settings()->value("enhancementIntensity").toDouble()==1);
        rendererTab->forceActiveFocus(Qt::TabFocusReason);
        CHECK(capture("renderer-three-passes"));
        window.resize(540, 720); wrapperItem->setHeight(720);
        CHECK(capture("renderer-three-passes-720"));
        CHECK(item->height() <= 720);
        auto sharpReset = live->findChild<QQuickItem *>("resetNrSharpness");
        CHECK(sharpReset);
        sharpReset->forceActiveFocus(Qt::TabFocusReason);
        QTest::qWait(30);
        const auto resetPosition = sharpReset->mapToItem(item, QPointF(0, 0));
        CHECK(resetPosition.y() >= 0 && resetPosition.y() + sharpReset->height() <= item->height() + 1);
        auto liveFsr = live->findChild<QQuickItem *>("fsrToggle");
        CHECK(liveFsr);
        const auto fsrPosition = liveFsr->mapToItem(item, QPointF(0, 0));
        CHECK(fsrPosition.x() >= 0 && fsrPosition.x() + liveFsr->width() <= item->width() + 1);
        window.resize(540, 1080); wrapperItem->setHeight(1080);
        app.processEvents();
        auto livePassSelector = live->findChild<QQuickItem *>("nrEditingPass");
        CHECK(livePassSelector && livePassSelector->isVisible());
        livePassSelector->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(&window, Qt::Key_Return);
        app.processEvents();
        CHECK(livePassSelector->property("popup").value<QObject *>()->property("visible").toBool());
        QTest::keyClick(&window, Qt::Key_Down);
        QTest::keyClick(&window, Qt::Key_Return);
        app.processEvents();
        CHECK(backend.settings()->value("enhancementEditingPass").toInt() == 2);
        rendererTab->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(&window, Qt::Key_PageDown);
        CHECK(capture("eye-candy"));
        backend.window()->insert("enhancementApplying", true);
        CHECK(capture("applying"));
    }
    item->setParentItem(nullptr);
    wrapperItem->setParentItem(nullptr);
    qInfo() << "PASS: QML and focus navigation; Tab/Shift+Tab; L1/R1 key mapping; D-pad/confirm; resets; 50% backdrop";
    return 0;
}
#include "qml.moc"
