import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import org.streetpea.chiaking
import "controls" as C

ScrollView {
    id: view
    objectName: "eyeCandySettingsView"
    property alias firstControl: eyeEnabled
    property Item previousFocusItem
    property Item nextFocusItem
    readonly property real naturalContentHeight: settingsColumn.implicitHeight
    implicitHeight: naturalContentHeight
    clip: true
    contentHeight: naturalContentHeight
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    ScrollBar.vertical.policy: ScrollBar.AsNeeded
    contentWidth: availableWidth
    Connections {
        target: view.Window.window
        function onActiveFocusItemChanged() {
            const item = view.Window.window.activeFocusItem;
            let ancestor = item;
            while (ancestor && ancestor !== view) ancestor = ancestor.parent;
            if (!ancestor || !view.contentItem || !item) return;
            const top = item.mapToItem(view.contentItem, 0, 0).y + view.contentItem.contentY;
            const bottom = top + item.height;
            const maxY = Math.max(0, view.contentItem.contentHeight - view.height);
            if (top < view.contentItem.contentY)
                view.contentItem.contentY = Math.max(0, top);
            else if (bottom > view.contentItem.contentY + view.height)
                view.contentItem.contentY = Math.min(maxY, bottom - view.height);
        }
    }
    ColumnLayout {
        id: settingsColumn
        width: view.availableWidth
        spacing: 12
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("Fine-tune streamed video. Exposure and Dark Area apply to SDR video.")
        }
        C.CheckBox {
            id: eyeEnabled
            KeyNavigation.priority: KeyNavigation.BeforeItem
            KeyNavigation.up: view.previousFocusItem
            KeyNavigation.backtab: view.previousFocusItem
            KeyNavigation.down: checked ? eyePreset : view.nextFocusItem
            KeyNavigation.tab: checked ? eyePreset : view.nextFocusItem
            objectName: "eyeCandyEnabled"
            text: qsTr("Enable color adjustment")
            checked: Chiaki.settings.placeboColorAdjustmentEnabled
            onToggled: Chiaki.settings.placeboColorAdjustmentEnabled = checked
        }
        RowLayout {
            Label { text: qsTr("Color preset") }
            C.ComboBox {
                id: eyePreset
                objectName: "eyeCandyPreset"
                KeyNavigation.priority: KeyNavigation.BeforeItem
                KeyNavigation.down: popup.visible ? null : (currentIndex === 0 ? brightnessSlider : view.nextFocusItem)
                KeyNavigation.tab: currentIndex === 0 ? brightnessSlider : view.nextFocusItem
                model: [qsTr("Custom"), qsTr("Neutral")]
                currentIndex: Chiaki.settings.placeboColorAdjustmentPreset
                enabled: Chiaki.settings.placeboColorAdjustmentEnabled
                onActivated: index => Chiaki.settings.placeboColorAdjustmentPreset = index
            }
        }
        GridLayout {
            columns: 4
            columnSpacing: 12
            enabled: Chiaki.settings.placeboColorAdjustmentEnabled && Chiaki.settings.placeboColorAdjustmentPreset === 0
            Label { text: qsTr("Brightness") }
            C.Slider {
                id: brightnessSlider
                objectName: "eyeCandyBrightness"
                Layout.preferredWidth: 175
                from: -0.14; to: 0.14; stepSize: 0.01
                value: Chiaki.settings.placeboColorAdjustmentBrightness
                onMoved: Chiaki.settings.placeboColorAdjustmentBrightness = value
            }
            C.Button {
                flat: true
                focusPolicy: Qt.StrongFocus
                objectName: "resetEyeCandyBrightness"
                text: "\u21ba"
                font.pixelSize: 24
                Layout.preferredWidth: 36
                Layout.preferredHeight: 36
                Accessible.name: qsTr("Reset brightness")
                ToolTip.visible: view.visible && (hovered || activeFocus)
                ToolTip.text: qsTr("Reset brightness to 0.00")
                onClicked: Chiaki.settings.placeboColorAdjustmentBrightness = 0
            }
            Label { text: Chiaki.settings.placeboColorAdjustmentBrightness.toFixed(2) }
            Label { text: qsTr("Contrast") }
            C.Slider {
                objectName: "eyeCandyContrast"
                Layout.preferredWidth: 175
                from: 0; to: 14.3; stepSize: 0.1
                value: Chiaki.settings.placeboColorAdjustmentContrast
                onMoved: Chiaki.settings.placeboColorAdjustmentContrast = value
            }
            C.Button {
                flat: true
                focusPolicy: Qt.StrongFocus
                objectName: "resetEyeCandyContrast"
                text: "\u21ba"
                font.pixelSize: 24
                Layout.preferredWidth: 36
                Layout.preferredHeight: 36
                Accessible.name: qsTr("Reset contrast")
                ToolTip.visible: view.visible && (hovered || activeFocus)
                ToolTip.text: qsTr("Reset contrast to 1.0")
                onClicked: Chiaki.settings.placeboColorAdjustmentContrast = 1
            }
            Label { text: Chiaki.settings.placeboColorAdjustmentContrast.toFixed(1) }
            Label { text: qsTr("Saturation") }
            C.Slider {
                objectName: "eyeCandySaturation"
                Layout.preferredWidth: 175
                from: 0; to: 14.3; stepSize: 0.1
                value: Chiaki.settings.placeboColorAdjustmentSaturation
                onMoved: Chiaki.settings.placeboColorAdjustmentSaturation = value
            }
            C.Button {
                flat: true
                focusPolicy: Qt.StrongFocus
                objectName: "resetEyeCandySaturation"
                text: "\u21ba"
                font.pixelSize: 24
                Layout.preferredWidth: 36
                Layout.preferredHeight: 36
                Accessible.name: qsTr("Reset saturation")
                ToolTip.visible: view.visible && (hovered || activeFocus)
                ToolTip.text: qsTr("Reset saturation to 1.00")
                onClicked: Chiaki.settings.placeboColorAdjustmentSaturation = 1
            }
            Label { text: Chiaki.settings.placeboColorAdjustmentSaturation.toFixed(2) }
            Label { text: qsTr("Gamma") }
            C.Slider {
                objectName: "eyeCandyGamma"
                Layout.preferredWidth: 175
                from: 0; to: 14.3; stepSize: 0.1
                value: Chiaki.settings.placeboColorAdjustmentGamma
                onMoved: Chiaki.settings.placeboColorAdjustmentGamma = value
            }
            C.Button {
                flat: true
                focusPolicy: Qt.StrongFocus
                objectName: "resetEyeCandyGamma"
                text: "\u21ba"
                font.pixelSize: 24
                Layout.preferredWidth: 36
                Layout.preferredHeight: 36
                Accessible.name: qsTr("Reset gamma")
                ToolTip.visible: view.visible && (hovered || activeFocus)
                ToolTip.text: qsTr("Reset gamma to 1.0")
                onClicked: Chiaki.settings.placeboColorAdjustmentGamma = 1
            }
            Label { text: Chiaki.settings.placeboColorAdjustmentGamma.toFixed(1) }
            Label { text: qsTr("Exposure") }
            C.Slider {
                objectName: "eyeCandyExposure"
                Layout.preferredWidth: 175
                from: -100; to: 100; stepSize: 5
                value: Chiaki.settings.eyeCandyExposure
                onMoved: Chiaki.settings.eyeCandyExposure = Math.round(value)
                Accessible.name: qsTr("Exposure")
                ToolTip.visible: view.visible && (hovered || activeFocus)
                ToolTip.text: qsTr("Overall exposure in stops; 0 is neutral")
            }
            C.Button {
                objectName: "resetEyeCandyExposure"
                flat: true
                focusPolicy: Qt.StrongFocus
                text: "\u21ba"
                font.pixelSize: 24
                Layout.preferredWidth: 36
                Layout.preferredHeight: 36
                Accessible.name: qsTr("Reset exposure")
                ToolTip.visible: view.visible && (hovered || activeFocus)
                ToolTip.text: qsTr("Reset exposure to 0")
                onClicked: Chiaki.settings.eyeCandyExposure = 0
            }
            Label { text: (Chiaki.settings.eyeCandyExposure / 100).toFixed(2) + " EV" }

            Label { text: qsTr("Dark Area") }
            C.Slider {
                objectName: "eyeCandyDarkArea"
                Layout.preferredWidth: 175
                from: -100; to: 100; stepSize: 5
                value: Chiaki.settings.eyeCandyDarkArea
                onMoved: Chiaki.settings.eyeCandyDarkArea = Math.round(value)
                Accessible.name: qsTr("Dark Area")
                ToolTip.visible: view.visible && (hovered || activeFocus)
                ToolTip.text: qsTr("Lift or deepen shadows; highlights stay unchanged when exposure is neutral")
            }
            C.Button {
                objectName: "resetEyeCandyDarkArea"
                KeyNavigation.priority: KeyNavigation.BeforeItem
                KeyNavigation.down: view.nextFocusItem
                KeyNavigation.tab: view.nextFocusItem
                flat: true
                focusPolicy: Qt.StrongFocus
                text: "\u21ba"
                font.pixelSize: 24
                Layout.preferredWidth: 36
                Layout.preferredHeight: 36
                Accessible.name: qsTr("Reset dark area")
                ToolTip.visible: view.visible && (hovered || activeFocus)
                ToolTip.text: qsTr("Reset dark area to 0")
                onClicked: Chiaki.settings.eyeCandyDarkArea = 0
            }
            Label { text: (Chiaki.settings.eyeCandyDarkArea / 100).toFixed(2) + " EV" }

        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            visible: Math.abs(Chiaki.settings.placeboColorAdjustmentBrightness) > 0.14
                || Chiaki.settings.placeboColorAdjustmentContrast < 0 || Chiaki.settings.placeboColorAdjustmentContrast > 14.3
                || Chiaki.settings.placeboColorAdjustmentSaturation < 0 || Chiaki.settings.placeboColorAdjustmentSaturation > 14.3
                || Chiaki.settings.placeboColorAdjustmentGamma < 0 || Chiaki.settings.placeboColorAdjustmentGamma > 14.3
            text: qsTr("A saved value exceeds this tab’s fine adjustment range. Move its slider or use ↺ to reset it.")
        }

    }
}
