import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import org.streetpea.chiaking
import "controls" as C

ColumnLayout {
    id: bar
    objectName: "updateBar"
    property var updater: Chiaki.updates
    readonly property bool compact: width < 500
    signal installRequested()
    implicitWidth: 620
    spacing: 2
    Label {
        objectName: "updateStatus"
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignRight
        wrapMode: Text.WordWrap
        font.pixelSize: 14
        visible: bar.updater.busy || bar.updater.error
        text: bar.updater.status + (bar.updater.installing && bar.updater.progress < 1 ? " " + Math.round(bar.updater.progress * 100) + "%" : "")
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: bar.compact ? 6 : 10
        Item { Layout.fillWidth: true }
        Label { text: bar.compact ? qsTr("Updates:") : qsTr("Update channel:") }
        C.ComboBox {
            objectName: "updateChannel"
            Layout.preferredWidth: bar.compact ? 120 : 150
            model: [qsTr("Release"),qsTr("Pre-release")]
            currentIndex: bar.updater.channel === "prerelease" ? 1 : 0
            enabled: !bar.updater.installing
            onActivated: (index) => bar.updater.channel = index === 1 ? "prerelease" : "release"
            Accessible.name: qsTr("Update channel")
            ToolTip.visible: hovered || activeFocus
            ToolTip.text: qsTr("Pre-release includes release versions and preview builds")
        }
        C.Button {
            objectName: "installUpdate"
            visible: bar.updater.available || bar.updater.installing
            enabled: !bar.updater.busy && bar.updater.installAllowed
            text: bar.updater.installing ? qsTr("Updating…") : (bar.compact ? qsTr("Update") : qsTr("Update to v%1").arg(bar.updater.availableVersion))
            Accessible.name: qsTr("Update to version %1").arg(bar.updater.availableVersion)
            ToolTip.visible: hovered || activeFocus
            ToolTip.text: !bar.updater.installAllowed ? qsTr("End your stream before updating") : bar.updater.status
            onClicked: bar.installRequested()
        }
        C.Button {
            objectName: "checkUpdates"
            visible: !bar.updater.available && !bar.updater.installing
            enabled: !bar.updater.busy
            text: bar.updater.busy ? "…" : "\u21bb"
            flat: true
            Layout.preferredWidth: 36
            Accessible.name: qsTr("Check for updates")
            ToolTip.visible: hovered || activeFocus
            ToolTip.text: bar.updater.status
            onClicked: bar.updater.checkForUpdates()
        }
        Label {
            objectName: "currentVersion"
            text: "v" + bar.updater.currentVersion
        }
    }
}
