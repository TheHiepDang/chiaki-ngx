import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import org.streetpea.chiaking
import "controls" as C

RowLayout {
    id: row
    objectName: "rtxSettingsShortcutRow"
    visible: Chiaki.window.rtxSettingsSupported
    enabled: Chiaki.window.rtxSettingsSupported
    signal captureRequested()
    signal focusRequested(var control)
    Label { objectName: "rtxSettingsShortcutLabel"; text: qsTr("RTX settings (during stream)") }
    C.Button {
        objectName: "rtxSettingsShortcutButton"
        text: Chiaki.settings.rendererShortcut
        onClicked: row.captureRequested()
        onActiveFocusChanged: if (activeFocus) row.focusRequested(this)
    }
    C.Button {
        text: qsTr("Reset")
        onClicked: Chiaki.settings.resetRendererShortcut()
    }
}
