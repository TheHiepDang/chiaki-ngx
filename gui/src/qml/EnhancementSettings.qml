import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Window
import org.streetpea.chiaking
import "controls" as C

ScrollView {
    id: view
    objectName: "rendererSettingsView"
    property bool compact: false
    readonly property Item firstControl: nrToggle.enabled ? nrToggle : gameplayStats
    property Item previousFocusItem
    property Item nextFocusItem
    readonly property bool customPasses: Chiaki.settings.enhancementCustomizePasses && Chiaki.settings.enhancementPassCount > 1
    readonly property bool nrEnabled: Chiaki.window.enhancementNeuralAvailable && Chiaki.settings.enhancementNeural
    readonly property bool showUpscaler: Chiaki.settings.enhancementPercent < 100
    readonly property bool showSharpness: showUpscaler && Chiaki.settings.enhancementFsr
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
    component ResetButton: C.Button {
        property string resetLabel
        text: "\u21ba"
        flat: true
        focusPolicy: Qt.StrongFocus
        font.pixelSize: 24
        Layout.preferredWidth: 36
        Layout.preferredHeight: 36
        enabled: view.nrEnabled
        Accessible.name: resetLabel
        ToolTip.visible: view.visible && (hovered || activeFocus)
        ToolTip.text: resetLabel
    }
    ColumnLayout {
        id: settingsColumn
        width: view.availableWidth
        spacing: view.compact ? 6 : 14
        Label {
            Layout.fillWidth: true
            Layout.margins: view.compact ? 4 : 16
            wrapMode: Text.WordWrap
            visible: !view.compact
            text: qsTr("Experimental Windows enhancements · SDR output")
        }
        GridLayout {
            Layout.alignment: Qt.AlignHCenter
            columns: 2
            rowSpacing: view.compact ? 6 : 12
            columnSpacing: 20
            enabled: !Chiaki.window.enhancementApplying
            Label { text: qsTr("Neural Rendering") }
            C.CheckBox {
                id: nrToggle
                KeyNavigation.priority: KeyNavigation.BeforeItem
                KeyNavigation.up: view.previousFocusItem
                KeyNavigation.backtab: view.previousFocusItem
                objectName: "nrToggle"
                text: qsTr("Enable (experimental)")
                enabled: Chiaki.window.enhancementNeuralAvailable
                checked: Chiaki.settings.enhancementNeural && Chiaki.window.enhancementNeuralAvailable
                onToggled: Chiaki.settings.enhancementNeural = checked
            }
            Label { text: qsTr("NR passes"); visible: Chiaki.settings.enhancementNeural }
            C.ComboBox {
                objectName: "nrPassCount"
                visible: Chiaki.settings.enhancementNeural
                enabled: Chiaki.window.enhancementNeuralAvailable
                Layout.preferredWidth: view.compact ? 200 : 250
                model: [qsTr("1 pass"), qsTr("2 passes"), qsTr("3 passes")]
                currentIndex: Chiaki.settings.enhancementPassCount - 1
                onActivated: (index) => Chiaki.settings.enhancementPassCount = index + 1
            }
            Label { text: qsTr("Pass settings"); visible: Chiaki.settings.enhancementNeural && Chiaki.settings.enhancementPassCount > 1 }
            C.CheckBox {
                objectName: "nrCustomizePasses"
                visible: Chiaki.settings.enhancementNeural && Chiaki.settings.enhancementPassCount > 1
                enabled: view.nrEnabled
                text: qsTr("Customize each pass")
                checked: Chiaki.settings.enhancementCustomizePasses
                onToggled: Chiaki.settings.enhancementCustomizePasses = checked
            }
            Label { text: qsTr("Configure pass"); visible: Chiaki.settings.enhancementNeural && view.customPasses }
            C.ComboBox {
                objectName: "nrEditingPass"
                visible: Chiaki.settings.enhancementNeural && view.customPasses
                enabled: Chiaki.window.enhancementNeuralAvailable
                Layout.preferredWidth: view.compact ? 200 : 250
                model: [qsTr("Pass 1"), qsTr("Pass 2"), qsTr("Pass 3")].slice(0, Chiaki.settings.enhancementPassCount)
                currentIndex: Chiaki.settings.enhancementEditingPass
                onActivated: (index) => Chiaki.settings.enhancementEditingPass = index
            }
            Label {
                Layout.columnSpan: 2
                visible: Chiaki.settings.enhancementNeural && Chiaki.settings.enhancementPassCount > 1 && !view.customPasses
                text: qsTr("All passes follow pass 1")
            }
            Label { text: qsTr("NR preset") }
            C.ComboBox {
                objectName: "nrPreset"
                enabled: view.nrEnabled
                Layout.preferredWidth: view.compact ? 200 : 250
                model: [qsTr("Default"), qsTr("Natural"), qsTr("Cinematic")]
                currentIndex: Chiaki.settings.enhancementNrPreset
                onActivated: (index) => Chiaki.settings.enhancementNrPreset = index
            }
            Label { text: qsTr("NR Intensity") }
            RowLayout {
                C.Slider {
                    objectName: "nrIntensity"
                    Layout.preferredWidth: view.compact ? 200 : 250
                    enabled: view.nrEnabled
                    from: 0; to: 1; stepSize: 0.05
                    value: Chiaki.settings.enhancementIntensity
                    onMoved: Chiaki.settings.enhancementIntensity = Math.round(value * 20) / 20
                }
                ResetButton {
                    objectName: "resetNrIntensity"
                    resetLabel: qsTr("Reset NR intensity to 1.00")
                    onClicked: Chiaki.settings.enhancementIntensity = 1
                }
                Label { text: Number(Chiaki.settings.enhancementIntensity).toFixed(2) }
            }
            Label { text: qsTr("NR Local Structure") }
            RowLayout {
                C.Slider {
                    objectName: "nrLocalStructure"
                    Layout.preferredWidth: view.compact ? 200 : 250
                    enabled: view.nrEnabled
                    from: 0; to: 2; stepSize: 0.05
                    value: Chiaki.settings.enhancementLocalStructure
                    onMoved: Chiaki.settings.enhancementLocalStructure = Math.round(value * 20) / 20
                }
                ResetButton {
                    objectName: "resetNrLocalStructure"
                    resetLabel: qsTr("Reset NR local structure to 1.00")
                    onClicked: Chiaki.settings.enhancementLocalStructure = 1
                }
                Label { text: Number(Chiaki.settings.enhancementLocalStructure).toFixed(2) }
            }
            Label { text: qsTr("NR Local Tone") }
            RowLayout {
                C.Slider {
                    objectName: "nrLocalTone"
                    Layout.preferredWidth: view.compact ? 200 : 250
                    enabled: view.nrEnabled
                    from: 0; to: 2; stepSize: 0.05
                    value: Chiaki.settings.enhancementLocalTone
                    onMoved: Chiaki.settings.enhancementLocalTone = Math.round(value * 20) / 20
                }
                ResetButton {
                    objectName: "resetNrLocalTone"
                    resetLabel: qsTr("Reset NR local tone to 1.00")
                    onClicked: Chiaki.settings.enhancementLocalTone = 1
                }
                Label { text: Number(Chiaki.settings.enhancementLocalTone).toFixed(2) }
            }
            Label { text: qsTr("NR Automatic Mask") }
            C.CheckBox {
                objectName: "nrAutomaticMask"
                text: qsTr("Enable")
                enabled: view.nrEnabled
                checked: Chiaki.settings.enhancementAutomaticMask
                onToggled: Chiaki.settings.enhancementAutomaticMask = checked
            }
            Label { text: qsTr("NR Skin Structure") }
            RowLayout {
                C.Slider {
                    objectName: "nrSkinStructure"
                    Layout.preferredWidth: view.compact ? 200 : 250
                    enabled: view.nrEnabled && Chiaki.settings.enhancementAutomaticMask
                    from: -1; to: 2.5; stepSize: 0.05
                    value: Chiaki.settings.enhancementSkinStructure
                    onMoved: Chiaki.settings.enhancementSkinStructure = Math.round(value * 20) / 20
                }
                ResetButton {
                    objectName: "resetNrSkinStructure"
                    resetLabel: qsTr("Reset NR skin structure to Off")
                    onClicked: Chiaki.settings.enhancementSkinStructure = -1
                }
                Label { text: Chiaki.settings.enhancementSkinStructure <= -1 ? qsTr("Off") : Number(Chiaki.settings.enhancementSkinStructure).toFixed(2) }
            }
            Label {
                Layout.columnSpan: 2
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                visible: !Chiaki.settings.enhancementAutomaticMask
                text: qsTr("Skin Structure requires Automatic Mask")
                font.pixelSize: 14
            }
            Label { text: qsTr("NR resolution") }
            RowLayout {
                C.Slider {
                    Layout.preferredWidth: view.compact ? 200 : 250
                    from: 50; to: 100; stepSize: 1
                    enabled: Chiaki.window.enhancementNeuralAvailable && Chiaki.settings.enhancementNeural
                    objectName: "nrScale"
                    value: Chiaki.settings.enhancementPercentPreview
                    onMoved: Chiaki.settings.queueEnhancementPercent(Math.round(value))
                }
                ResetButton {
                    objectName: "resetNrResolution"
                    resetLabel: qsTr("Reset NR resolution to 65% (applies after 3 seconds)")
                    onClicked: Chiaki.settings.queueEnhancementPercent(65)
                }
                Label { text: Chiaki.settings.enhancementPercentPreview + "%" }
            }
            Label {
                Layout.columnSpan: 2
                Layout.alignment: Qt.AlignHCenter
                visible: Chiaki.settings.enhancementResolutionPending
                text: qsTr("Applies 3 seconds after the last adjustment")
            }
            Label { text: qsTr("FSR upscaling"); visible: view.showUpscaler }
            C.CheckBox {
                objectName: "fsrToggle"
                visible: view.showUpscaler
                text: qsTr("Reconstruct reduced output")
                checked: Chiaki.settings.enhancementFsr
                enabled: Chiaki.window.enhancementNeuralAvailable && Chiaki.settings.enhancementNeural && Chiaki.window.enhancementUpscalingEligible
                onToggled: Chiaki.settings.enhancementFsr = checked
            }
            Label { text: qsTr("Sharpness"); visible: view.showSharpness }
            RowLayout {
                objectName: "sharpnessRow"
                visible: view.showSharpness
                C.Slider {
                    Layout.preferredWidth: view.compact ? 200 : 250
                    from: 0; to: 100; stepSize: 1
                    enabled: Chiaki.window.enhancementNeuralAvailable && Chiaki.settings.enhancementNeural && Chiaki.settings.enhancementFsr && Chiaki.window.enhancementUpscalingEligible
                    objectName: "sharpness"
                    value: Chiaki.settings.enhancementSharpness
                    onMoved: Chiaki.settings.enhancementSharpness = Math.round(value)
                }
                ResetButton {
                    objectName: "resetNrSharpness"
                    resetLabel: qsTr("Reset FSR sharpness to 20%")
                    enabled: view.nrEnabled && Chiaki.settings.enhancementFsr && Chiaki.window.enhancementUpscalingEligible
                    onClicked: Chiaki.settings.enhancementSharpness = 20
                }
                Label { text: Chiaki.settings.enhancementSharpness + "%" }
            }
            Label { text: qsTr("Gameplay statistics") }
            C.CheckBox {
                id: gameplayStats
                objectName: "gameplayStatsToggle"
                text: qsTr("Show FPS overlay")
                checked: Chiaki.settings.showStreamStats
                onToggled: Chiaki.settings.showStreamStats = checked
            }
            Label { text: qsTr("Frame generation") }
            C.CheckBox {
                text: qsTr("2× (experimental)")
                objectName: "fgToggle"
                KeyNavigation.priority: KeyNavigation.BeforeItem
                KeyNavigation.down: view.nextFocusItem
                KeyNavigation.tab: view.nextFocusItem
                checked: Chiaki.settings.enhancementFrameGeneration
                onToggled: Chiaki.settings.enhancementFrameGeneration = checked
            }
        }
        Label {
            objectName: "rendererDimensions"
            visible: !view.compact
            Layout.fillWidth: true
            Layout.margins: view.compact ? 4 : 16
            wrapMode: Text.WordWrap
            text: Chiaki.window.enhancementDimensions + "\n" + Chiaki.window.enhancementCadence
        }
        Label {
            objectName: "rendererStatus"
            visible: !view.compact
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            wrapMode: Text.WordWrap
            text: Chiaki.window.enhancementStatus
        }
        Label {
            objectName: "rendererActive"
            visible: !view.compact
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            wrapMode: Text.WordWrap
            text: qsTr("Active: NR %1 · FSR %2 · FG %3")
                .arg(Chiaki.window.enhancementNrActive ? qsTr("On") : qsTr("Off"))
                .arg(Chiaki.window.enhancementFsrActive ? qsTr("On") : qsTr("Off"))
                .arg(Chiaki.window.enhancementFgActive ? qsTr("On") : qsTr("Off"))
        }
        Label {
            Layout.fillWidth: true
            Layout.margins: view.compact ? 4 : 16
            wrapMode: Text.WordWrap
            visible: !view.compact
            text: qsTr("Each NR pass enhances the preceding pass. Extra passes follow pass 1 until Customize each pass is enabled. NR presets select the model without changing your strength sliders. Skin Structure uses Automatic Mask; -1 turns skin adjustments off. Resolution is relative to the displayed video area. Additional passes increase GPU time and memory use. Optional NVIDIA runtimes are installed separately; unavailable features leave ordinary playback running.")
        }
    }
}
