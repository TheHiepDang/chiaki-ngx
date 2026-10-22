import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import org.streetpea.chiaking
import "controls" as C

ScrollView {
    id: view
    objectName: "rendererSettingsView"
    property bool compact: false
    readonly property Item firstControl: nrToggle.enabled ? nrToggle : gameplayStats
    property Item previousFocusItem
    property Item nextFocusItem
    readonly property bool showUpscaler: Chiaki.settings.enhancementPercent < 100
    readonly property bool showSharpness: showUpscaler && Chiaki.settings.enhancementFsr
    readonly property real naturalContentHeight: settingsColumn.implicitHeight
    implicitHeight: naturalContentHeight
    clip: true
    contentHeight: naturalContentHeight
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    ScrollBar.vertical.policy: ScrollBar.AsNeeded
    contentWidth: availableWidth
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
                Label { text: Chiaki.settings.enhancementPercentPreview + "%" }
            }
            Label {
                Layout.columnSpan: 2
                Layout.alignment: Qt.AlignHCenter
                visible: Chiaki.settings.enhancementPercentPreview !== Chiaki.settings.enhancementPercent
                text: qsTr("Applies 3 seconds after the last adjustment")
            }
            Label { text: qsTr("FSR upscaling"); visible: view.showUpscaler }
            C.CheckBox {
                objectName: "fsrToggle"
                visible: view.showUpscaler
                text: qsTr("Reconstruct reduced NR output")
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
            text: qsTr("NR resolution is a percentage of the displayed video area. Reduced processing preserves original video detail. Optional NVIDIA runtimes are installed separately; unavailable features leave ordinary playback running.")
        }
    }
}
