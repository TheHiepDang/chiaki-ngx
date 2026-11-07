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
    property bool horizontalBar: false
    property Item previousFocusItem
    property Item nextFocusItem
    readonly property bool customPasses: Chiaki.settings.enhancementCustomizePasses && Chiaki.settings.enhancementPassCount > 1
    readonly property bool nrEnabled: Chiaki.window.enhancementNeuralAvailable && Chiaki.settings.enhancementNeural
    readonly property bool showUpscaler: Chiaki.settings.enhancementPercent < 100
    readonly property bool showSharpness: showUpscaler && Chiaki.settings.enhancementFsr
    readonly property real naturalContentHeight: settingsColumn.implicitHeight
    implicitWidth: 540
    implicitHeight: naturalContentHeight
    clip: true
    contentHeight: naturalContentHeight
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    readonly property bool canScrollLeft: horizontalBar && contentItem && contentItem.contentX > 1
    readonly property bool canScrollRight: horizontalBar && contentItem &&
        contentItem.contentX < Math.max(0, contentWidth - availableWidth) - 1
    function scrollOptions(direction) {
        const maxX = Math.max(0, contentWidth - availableWidth);
        contentItem.contentX = Math.max(0, Math.min(maxX,
            contentItem.contentX + direction * Math.max(120, availableWidth * 0.75)));
    }
    ScrollBar.vertical.policy: ScrollBar.AsNeeded
    contentWidth: horizontalBar ? Math.max(width - leftPadding - rightPadding, settingsColumn.implicitWidth) : availableWidth
    // A regular mouse wheel pans the strip; sliders keep arrow-key adjustment.
    WheelHandler {
        enabled: view.horizontalBar
        target: null
        onWheel: event => {
            const delta = event.pixelDelta.y || event.angleDelta.y / 2;
            const maxX = Math.max(0, view.contentWidth - view.availableWidth);
            view.contentItem.contentX = Math.max(0, Math.min(maxX, view.contentItem.contentX - delta));
            event.accepted = true;
        }
    }
    Connections {
        target: view.Window.window
        function onActiveFocusItemChanged() {
            const item = view.Window.window.activeFocusItem;
            let ancestor = item;
            while (ancestor && ancestor !== view) ancestor = ancestor.parent;
            if (!ancestor || !view.contentItem || !item) return;
            if (view.horizontalBar) {
                const left = item.mapToItem(view.contentItem, 0, 0).x + view.contentItem.contentX;
                const right = left + item.width;
                const maxX = Math.max(0, view.contentItem.contentWidth - view.availableWidth);
                if (left < view.contentItem.contentX)
                    view.contentItem.contentX = Math.max(0, left - 8);
                else if (right > view.contentItem.contentX + view.availableWidth)
                    view.contentItem.contentX = Math.min(maxX, right - view.availableWidth + 8);
            }
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
        navigationScope: view.horizontalBar ? view : null
        property string resetLabel
        text: "\u21ba"
        flat: true
        focusPolicy: Qt.StrongFocus
        font.pixelSize: view.horizontalBar ? 17 : 24
        leftPadding: 0
        rightPadding: 0
        topPadding: 0
        bottomPadding: 0
        Layout.preferredWidth: view.horizontalBar ? 26 : 36
        Layout.preferredHeight: view.horizontalBar ? 28 : 36
        enabled: view.nrEnabled
        Accessible.name: resetLabel
        ToolTip.visible: view.visible && (hovered || activeFocus)
        ToolTip.text: resetLabel
    }
    ColumnLayout {
        id: settingsColumn
        width: view.horizontalBar ? implicitWidth : view.availableWidth
        spacing: view.compact ? 6 : 14
        Label {
            Layout.fillWidth: true
            Layout.margins: view.compact ? 4 : 16
            wrapMode: Text.WordWrap
            visible: !view.compact
            text: qsTr("Experimental Windows enhancements · SDR and HDR NR")
        }
        GridLayout {
            Layout.alignment: view.horizontalBar ? Qt.AlignLeft | Qt.AlignTop : Qt.AlignHCenter
            columns: view.horizontalBar ? 100 : 1
            rowSpacing: view.compact ? 6 : 12
            columnSpacing: view.horizontalBar ? 12 : 20
            enabled: !Chiaki.window.enhancementApplying
            GridLayout {
                columns: view.horizontalBar ? 1 : 2
                Layout.alignment: Qt.AlignTop
                rowSpacing: 0
                columnSpacing: view.horizontalBar ? 12 : 20
                Layout.minimumWidth: view.horizontalBar ? 140 : 0
                Label { Layout.preferredWidth: view.horizontalBar ? -1 : 190; text: qsTr("Neural Rendering") }
                C.CheckBox {
                    navigationScope: view.horizontalBar ? view : null
                    Layout.preferredHeight: view.horizontalBar ? 32 : -1
                    id: nrToggle
                    KeyNavigation.priority: KeyNavigation.BeforeItem
                    KeyNavigation.up: view.previousFocusItem
                    KeyNavigation.backtab: view.previousFocusItem
                    objectName: "nrToggle"
                    text: qsTr("Enable")
                    enabled: Chiaki.window.enhancementNeuralAvailable
                    checked: Chiaki.settings.enhancementNeural && Chiaki.window.enhancementNeuralAvailable
                    onToggled: Chiaki.settings.enhancementNeural = checked
                }
            }
            GridLayout {
                columns: view.horizontalBar ? 1 : 2
                Layout.alignment: Qt.AlignTop
                rowSpacing: 0
                columnSpacing: view.horizontalBar ? 12 : 20
                visible: Chiaki.settings.enhancementNeural
                Layout.minimumWidth: view.horizontalBar ? 140 : 0
                Label { Layout.preferredWidth: view.horizontalBar ? -1 : 190; text: qsTr("NR passes")}
                C.ComboBox {
                    navigationScope: view.horizontalBar ? view : null
                    Layout.preferredHeight: view.horizontalBar ? 32 : -1
                    objectName: "nrPassCount"
                    visible: Chiaki.settings.enhancementNeural
                    enabled: Chiaki.window.enhancementNeuralAvailable
                    Layout.preferredWidth: view.horizontalBar ? 128 : (view.compact ? 200 : 250)
                    model: [qsTr("1 pass"), qsTr("2 passes"), qsTr("3 passes")]
                    currentIndex: Chiaki.settings.enhancementPassCount - 1
                    onActivated: (index) => Chiaki.settings.enhancementPassCount = index + 1
                }
            }
            GridLayout {
                columns: view.horizontalBar ? 1 : 2
                Layout.alignment: Qt.AlignTop
                rowSpacing: 0
                columnSpacing: view.horizontalBar ? 12 : 20
                visible: Chiaki.settings.enhancementNeural && Chiaki.settings.enhancementPassCount > 1
                Layout.minimumWidth: view.horizontalBar ? 140 : 0
                Label { Layout.preferredWidth: view.horizontalBar ? -1 : 190; text: qsTr("Pass settings")}
                C.CheckBox {
                    navigationScope: view.horizontalBar ? view : null
                    Layout.preferredHeight: view.horizontalBar ? 32 : -1
                    objectName: "nrCustomizePasses"
                    visible: Chiaki.settings.enhancementNeural && Chiaki.settings.enhancementPassCount > 1
                    enabled: view.nrEnabled
                    text: qsTr("Customize each pass")
                    checked: Chiaki.settings.enhancementCustomizePasses
                    onToggled: Chiaki.settings.enhancementCustomizePasses = checked
                }
            }
            GridLayout {
                columns: view.horizontalBar ? 1 : 2
                Layout.alignment: Qt.AlignTop
                rowSpacing: 0
                columnSpacing: view.horizontalBar ? 12 : 20
                visible: Chiaki.settings.enhancementNeural && view.customPasses
                Layout.minimumWidth: view.horizontalBar ? 140 : 0
                Label { Layout.preferredWidth: view.horizontalBar ? -1 : 190; text: qsTr("Configure pass")}
                C.ComboBox {
                    navigationScope: view.horizontalBar ? view : null
                    Layout.preferredHeight: view.horizontalBar ? 32 : -1
                    objectName: "nrEditingPass"
                    visible: Chiaki.settings.enhancementNeural && view.customPasses
                    enabled: Chiaki.window.enhancementNeuralAvailable
                    Layout.preferredWidth: view.horizontalBar ? 128 : (view.compact ? 200 : 250)
                    model: [qsTr("Pass 1"), qsTr("Pass 2"), qsTr("Pass 3")].slice(0, Chiaki.settings.enhancementPassCount)
                    currentIndex: Chiaki.settings.enhancementEditingPass
                    onActivated: (index) => Chiaki.settings.enhancementEditingPass = index
                }
            }
            Label {
                Layout.columnSpan: 1
                visible: !view.horizontalBar && Chiaki.settings.enhancementNeural && Chiaki.settings.enhancementPassCount > 1 && !view.customPasses
                text: qsTr("All passes follow pass 1")
            }
            GridLayout {
                columns: view.horizontalBar ? 1 : 2
                Layout.alignment: Qt.AlignTop
                rowSpacing: 0
                columnSpacing: view.horizontalBar ? 12 : 20
                Layout.minimumWidth: view.horizontalBar ? 140 : 0
                Label { Layout.preferredWidth: view.horizontalBar ? -1 : 190; text: qsTr("NR preset") }
                C.ComboBox {
                    navigationScope: view.horizontalBar ? view : null
                    Layout.preferredHeight: view.horizontalBar ? 32 : -1
                    objectName: "nrPreset"
                    enabled: view.nrEnabled
                    Layout.preferredWidth: view.horizontalBar ? 128 : (view.compact ? 200 : 250)
                    model: [qsTr("Default"), qsTr("Natural"), qsTr("Cinematic")]
                    currentIndex: Chiaki.settings.enhancementNrPreset
                    onActivated: (index) => Chiaki.settings.enhancementNrPreset = index
                }
            }
            GridLayout {
                columns: view.horizontalBar ? 1 : 2
                Layout.alignment: Qt.AlignTop
                rowSpacing: 0
                columnSpacing: view.horizontalBar ? 12 : 20
                Layout.minimumWidth: view.horizontalBar ? 140 : 0
                Label { Layout.preferredWidth: view.horizontalBar ? -1 : 190; text: qsTr("NR Intensity") }
                RowLayout {
                    C.Slider {
                        navigationScope: view.horizontalBar ? view : null
                        Layout.preferredHeight: view.horizontalBar ? 32 : -1
                        objectName: "nrIntensity"
                        Layout.preferredWidth: view.horizontalBar ? 128 : (view.compact ? 200 : 250)
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
            }
            GridLayout {
                columns: view.horizontalBar ? 1 : 2
                Layout.alignment: Qt.AlignTop
                rowSpacing: 0
                columnSpacing: view.horizontalBar ? 12 : 20
                Layout.minimumWidth: view.horizontalBar ? 140 : 0
                Label { Layout.preferredWidth: view.horizontalBar ? -1 : 190; text: qsTr("NR Local Structure") }
                RowLayout {
                    C.Slider {
                        navigationScope: view.horizontalBar ? view : null
                        Layout.preferredHeight: view.horizontalBar ? 32 : -1
                        objectName: "nrLocalStructure"
                        Layout.preferredWidth: view.horizontalBar ? 128 : (view.compact ? 200 : 250)
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
            }
            GridLayout {
                columns: view.horizontalBar ? 1 : 2
                Layout.alignment: Qt.AlignTop
                rowSpacing: 0
                columnSpacing: view.horizontalBar ? 12 : 20
                Layout.minimumWidth: view.horizontalBar ? 140 : 0
                Label { Layout.preferredWidth: view.horizontalBar ? -1 : 190; text: qsTr("NR Local Tone") }
                RowLayout {
                    C.Slider {
                        navigationScope: view.horizontalBar ? view : null
                        Layout.preferredHeight: view.horizontalBar ? 32 : -1
                        objectName: "nrLocalTone"
                        Layout.preferredWidth: view.horizontalBar ? 128 : (view.compact ? 200 : 250)
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
            }
            GridLayout {
                columns: view.horizontalBar ? 1 : 2
                Layout.alignment: Qt.AlignTop
                rowSpacing: 0
                columnSpacing: view.horizontalBar ? 12 : 20
                Layout.minimumWidth: view.horizontalBar ? 140 : 0
                Label { Layout.preferredWidth: view.horizontalBar ? -1 : 190; text: qsTr("NR Automatic Mask") }
                C.CheckBox {
                    navigationScope: view.horizontalBar ? view : null
                    Layout.preferredHeight: view.horizontalBar ? 32 : -1
                    objectName: "nrAutomaticMask"
                    text: qsTr("Enable")
                    enabled: view.nrEnabled
                    checked: Chiaki.settings.enhancementAutomaticMask
                    onToggled: Chiaki.settings.enhancementAutomaticMask = checked
                }
            }
            GridLayout {
                columns: view.horizontalBar ? 1 : 2
                Layout.alignment: Qt.AlignTop
                rowSpacing: 0
                columnSpacing: view.horizontalBar ? 12 : 20
                Layout.minimumWidth: view.horizontalBar ? 140 : 0
                Label { Layout.preferredWidth: view.horizontalBar ? -1 : 190; text: qsTr("NR Skin Structure") }
                RowLayout {
                    C.Slider {
                        navigationScope: view.horizontalBar ? view : null
                        Layout.preferredHeight: view.horizontalBar ? 32 : -1
                        objectName: "nrSkinStructure"
                        Layout.preferredWidth: view.horizontalBar ? 128 : (view.compact ? 200 : 250)
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
            }
            Label {
                Layout.columnSpan: 1
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                visible: !view.horizontalBar && !Chiaki.settings.enhancementAutomaticMask
                text: qsTr("Skin Structure requires Automatic Mask")
                font.pixelSize: 14
            }
            GridLayout {
                columns: view.horizontalBar ? 1 : 2
                Layout.alignment: Qt.AlignTop
                rowSpacing: 0
                columnSpacing: view.horizontalBar ? 12 : 20
                Layout.minimumWidth: view.horizontalBar ? 140 : 0
                Label { Layout.preferredWidth: view.horizontalBar ? -1 : 190; text: qsTr("NR resolution") }
                RowLayout {
                    C.Slider {
                        navigationScope: view.horizontalBar ? view : null
                        Layout.preferredHeight: view.horizontalBar ? 32 : -1
                        Layout.preferredWidth: view.horizontalBar ? 128 : (view.compact ? 200 : 250)
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
            }
            Label {
                Layout.columnSpan: 1
                Layout.alignment: Qt.AlignHCenter
                visible: !view.horizontalBar && Chiaki.settings.enhancementResolutionPending
                text: qsTr("Applies 3 seconds after the last adjustment")
            }
            GridLayout {
                columns: view.horizontalBar ? 1 : 2
                Layout.alignment: Qt.AlignTop
                rowSpacing: 0
                columnSpacing: view.horizontalBar ? 12 : 20
                visible: view.showUpscaler
                Layout.minimumWidth: view.horizontalBar ? 140 : 0
                Label { Layout.preferredWidth: view.horizontalBar ? -1 : 190; text: qsTr("FSR upscaling")}
                C.CheckBox {
                    navigationScope: view.horizontalBar ? view : null
                    Layout.preferredHeight: view.horizontalBar ? 32 : -1
                    objectName: "fsrToggle"
                    visible: view.showUpscaler
                    text: qsTr("Reconstruct reduced output")
                    checked: Chiaki.settings.enhancementFsr
                    enabled: Chiaki.window.enhancementNeuralAvailable && Chiaki.settings.enhancementNeural && Chiaki.window.enhancementUpscalingEligible
                    onToggled: Chiaki.settings.enhancementFsr = checked
                }
            }
            GridLayout {
                columns: view.horizontalBar ? 1 : 2
                Layout.alignment: Qt.AlignTop
                rowSpacing: 0
                columnSpacing: view.horizontalBar ? 12 : 20
                visible: view.showSharpness
                Layout.minimumWidth: view.horizontalBar ? 140 : 0
                Label { Layout.preferredWidth: view.horizontalBar ? -1 : 190; text: qsTr("Sharpness")}
                RowLayout {
                    objectName: "sharpnessRow"
                    visible: view.showSharpness
                    C.Slider {
                        navigationScope: view.horizontalBar ? view : null
                        Layout.preferredHeight: view.horizontalBar ? 32 : -1
                        Layout.preferredWidth: view.horizontalBar ? 128 : (view.compact ? 200 : 250)
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
            }
            GridLayout {
                columns: view.horizontalBar ? 1 : 2
                Layout.alignment: Qt.AlignTop
                rowSpacing: 0
                columnSpacing: view.horizontalBar ? 12 : 20
                Layout.minimumWidth: view.horizontalBar ? 140 : 0
                Label { Layout.preferredWidth: view.horizontalBar ? -1 : 190; text: qsTr("Gameplay statistics") }
                C.CheckBox {
                    navigationScope: view.horizontalBar ? view : null
                    Layout.preferredHeight: view.horizontalBar ? 32 : -1
                    id: gameplayStats
                    objectName: "gameplayStatsToggle"
                    text: qsTr("Show FPS overlay")
                    checked: Chiaki.settings.showStreamStats
                    onToggled: Chiaki.settings.showStreamStats = checked
                }
            }
            GridLayout {
                columns: view.horizontalBar ? 1 : 2
                Layout.alignment: Qt.AlignTop
                rowSpacing: 0
                columnSpacing: view.horizontalBar ? 12 : 20
                Layout.minimumWidth: view.horizontalBar ? 140 : 0
                Label { Layout.preferredWidth: view.horizontalBar ? -1 : 190; text: qsTr("Frame generation") }
                C.CheckBox {
                    navigationScope: view.horizontalBar ? view : null
                    Layout.preferredHeight: view.horizontalBar ? 32 : -1
                    text: qsTr("2× (experimental)")
                    objectName: "fgToggle"
                    KeyNavigation.priority: KeyNavigation.BeforeItem
                    KeyNavigation.down: view.horizontalBar ? null : view.nextFocusItem
                    KeyNavigation.tab: view.nextFocusItem
                    checked: Chiaki.settings.enhancementFrameGeneration
                    onToggled: Chiaki.settings.enhancementFrameGeneration = checked
                }
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
