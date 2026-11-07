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
    // A fixed, shallow strip keeps the game visible as options change.
    implicitHeight: 116
    readonly property int textSize: 14
    readonly property Item currentView: bar.currentIndex === 0 ? enhancements : eyeCandy
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
        color: Qt.rgba(Material.background.r, Material.background.g, Material.background.b, 0.88)
    }
    Keys.onEscapePressed: root.closeDialog()
    ColumnLayout {
        visible: !dialog.applying
        anchors.fill: parent
        anchors.margins: 6
        spacing: 4
        RowLayout {
            id: header
            Layout.fillWidth: true
            TabBar {
                id: bar
                objectName: "rendererTabs"
                Layout.preferredWidth: Math.min(260, dialog.width - closeButton.implicitWidth - 24)
                Layout.preferredHeight: 32
                background: Item { }
                TabButton {
                    id: rendererTab
                    objectName: "rendererTab"
                    text: qsTr("RTX settings")
                    font.pixelSize: dialog.textSize
                    implicitHeight: 32
                    focusPolicy: Qt.StrongFocus
                    onActiveFocusChanged: if (activeFocus) bar.currentIndex = 0
                    KeyNavigation.tab: eyeCandyTab
                    KeyNavigation.backtab: closeButton
                    KeyNavigation.down: enhancements.firstControl
                    background: Rectangle {
                        color: parent.checked ? Qt.rgba(Material.accent.r, Material.accent.g, Material.accent.b, 0.18) : "transparent"
                        border.color: rendererTab.activeFocus ? Material.accent : "transparent"
                        border.width: 2
                    }
                }
                TabButton {
                    id: eyeCandyTab
                    objectName: "eyeCandyTab"
                    text: qsTr("Eye Candy")
                    font.pixelSize: dialog.textSize
                    implicitHeight: 32
                    focusPolicy: Qt.StrongFocus
                    onActiveFocusChanged: if (activeFocus) bar.currentIndex = 1
                    KeyNavigation.tab: closeButton
                    KeyNavigation.backtab: rendererTab
                    KeyNavigation.down: eyeCandy.firstControl
                    background: Rectangle {
                        color: parent.checked ? Qt.rgba(Material.accent.r, Material.accent.g, Material.accent.b, 0.18) : "transparent"
                        border.color: eyeCandyTab.activeFocus ? Material.accent : "transparent"
                        border.width: 2
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: 0
                visible: dialog.width > 650
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideRight
                font.pixelSize: 12
                opacity: 0.65
                text: qsTr("←/→ Move · Confirm Adjust · ↑ Tabs")
            }
            C.Button {
                id: closeButton
                objectName: "rendererDialogClose"
                text: qsTr("Close")
                font.pixelSize: dialog.textSize
                Layout.preferredHeight: 32
                KeyNavigation.tab: bar.currentIndex === 0 ? enhancements.firstControl : eyeCandy.firstControl
                KeyNavigation.backtab: eyeCandyTab
                onClicked: root.closeDialog()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 2
            ToolButton {
                objectName: "rendererScrollLeft"
                text: "‹"
                font.pixelSize: 28
                Layout.preferredWidth: 24
                Layout.fillHeight: true
                focusPolicy: Qt.NoFocus
                enabled: dialog.currentView.canScrollLeft
                opacity: enabled ? 1 : 0.25
                Material.foreground: Material.accent
                background: Item { }
                Accessible.name: qsTr("Scroll options left")
                onClicked: dialog.currentView.scrollOptions(-1)
            }
        StackLayout {
            currentIndex: bar.currentIndex
            Layout.fillWidth: true
            Layout.fillHeight: true
            EnhancementSettings { id: enhancements; font.pixelSize: dialog.textSize; compact: true; horizontalBar: true; previousFocusItem: rendererTab; nextFocusItem: rendererTab }
            EyeCandySettings { id: eyeCandy; font.pixelSize: dialog.textSize; horizontalBar: true; previousFocusItem: eyeCandyTab; nextFocusItem: rendererTab }
        }
            ToolButton {
                objectName: "rendererScrollRight"
                text: "›"
                font.pixelSize: 28
                Layout.preferredWidth: 24
                Layout.fillHeight: true
                focusPolicy: Qt.NoFocus
                enabled: dialog.currentView.canScrollRight
                opacity: enabled ? 1 : 0.25
                Material.foreground: Material.accent
                background: Item { }
                Accessible.name: qsTr("Scroll options right")
                onClicked: dialog.currentView.scrollOptions(1)
            }
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
            Label { text: qsTr("Applying settings…"); font.pixelSize: dialog.textSize }
        }
    }
}
