import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Material
import org.streetpea.chiaking
import "controls" as C

FocusScope {
    id: dialog
    visible: Chiaki.window.rtxSettingsSupported
    enabled: Chiaki.window.rtxSettingsSupported
    focus: true
    implicitHeight: Math.ceil(header.implicitHeight + 4 + 20
        + (bar.currentIndex === 0 ? enhancements.naturalContentHeight : eyeCandy.naturalContentHeight))
    readonly property bool applying: Chiaki.window.enhancementApplying
    onApplyingChanged: if (!applying && visible) Qt.callLater(focusInitialItem)
    property Item restoreFocusItem
    property alias mainItem: rendererTab
    function showEnhancements() { if (!restoreFocusItem) bar.currentIndex = 0; }
    function focusInitialItem() { bar.currentItem.forceActiveFocus(Qt.TabFocusReason); }
    function switchTab(direction) {
        if (Chiaki.window.enhancementApplying) return;
        bar.currentIndex = (bar.currentIndex + direction + bar.count) % bar.count;
        focusInitialItem();
    }
    Shortcut {
        sequences: ["PgUp", "Ctrl+Shift+Tab"]
        enabled: dialog.visible && dialog.activeFocus
        onActivated: dialog.switchTab(-1)
    }
    Shortcut {
        sequences: ["PgDown", "Ctrl+Tab"]
        enabled: dialog.visible && dialog.activeFocus
        onActivated: dialog.switchTab(1)
    }
    Rectangle {
        objectName: "rendererDialogBackground"
        anchors.fill: parent
        color: Qt.rgba(Material.background.r, Material.background.g, Material.background.b, 0.5)
    }
    Keys.onEscapePressed: root.closeDialog()
    ColumnLayout {
        visible: !dialog.applying
        anchors.fill: parent
        anchors.margins: 10
        spacing: 4
        RowLayout {
            id: header
            Layout.fillWidth: true
            TabBar {
                id: bar
                objectName: "rendererTabs"
                Layout.fillWidth: true
                background: Item { }
                TabButton {
                    id: rendererTab
                    objectName: "rendererTab"
                    text: qsTr("RTX settings")
                    focusPolicy: Qt.StrongFocus
                    onActiveFocusChanged: if (activeFocus) bar.currentIndex = 0
                    KeyNavigation.tab: eyeCandyTab
                    KeyNavigation.backtab: closeButton
                    KeyNavigation.down: enhancements.firstControl
                    background: Rectangle {
                        color: "transparent"
                        border.color: rendererTab.activeFocus ? Material.accent : "transparent"
                        border.width: 2
                    }
                }
                TabButton {
                    id: eyeCandyTab
                    objectName: "eyeCandyTab"
                    text: qsTr("Eye Candy")
                    focusPolicy: Qt.StrongFocus
                    onActiveFocusChanged: if (activeFocus) bar.currentIndex = 1
                    KeyNavigation.tab: closeButton
                    KeyNavigation.backtab: rendererTab
                    KeyNavigation.down: eyeCandy.firstControl
                    background: Rectangle {
                        color: "transparent"
                        border.color: eyeCandyTab.activeFocus ? Material.accent : "transparent"
                        border.width: 2
                    }
                }
            }
            Item { Layout.fillWidth: true }
            C.Button {
                id: closeButton
                objectName: "rendererDialogClose"
                text: qsTr("Close")
                KeyNavigation.tab: bar.currentIndex === 0 ? enhancements.firstControl : eyeCandy.firstControl
                KeyNavigation.backtab: eyeCandyTab
                onClicked: root.closeDialog()
            }
        }
        StackLayout {
            currentIndex: bar.currentIndex
            Layout.fillWidth: true
            Layout.fillHeight: true
            EnhancementSettings { id: enhancements; compact: true; previousFocusItem: closeButton; nextFocusItem: rendererTab }
            EyeCandySettings { id: eyeCandy; previousFocusItem: closeButton; nextFocusItem: rendererTab }
        }
    }
    Rectangle {
        anchors.fill: parent
        visible: Chiaki.window.enhancementApplying
        color: "transparent"
        MouseArea { anchors.fill: parent }
        ColumnLayout {
            anchors.centerIn: parent
            BusyIndicator { Layout.alignment: Qt.AlignHCenter; running: parent.parent.visible }
            Label { text: qsTr("Applying settings…"); font.pixelSize: 20 }
        }
    }
}
