import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import "BarNavigation.js" as BarNavigation

Slider {
    id: control
    property Item navigationScope: null
    property bool barEditing: false
    onActiveFocusChanged: if (!activeFocus) barEditing = false
    ToolTip.visible: navigationScope && (hovered || activeFocus)
    ToolTip.text: barEditing ? qsTr("Left/Right: adjust · Confirm or Back: finish")
                            : qsTr("Left/Right: move · Confirm: adjust")
    Keys.onShortcutOverride: event => {
        if (navigationScope && barEditing && event.key === Qt.Key_Escape) event.accepted = true;
    }
    Keys.onReleased: event => {
        if (navigationScope && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space))
            event.accepted = true;
    }
    property bool firstInFocusChain: false
    property bool lastInFocusChain: false
    property bool sendOutput: false

    Keys.onPressed: (event) => {
        if (BarNavigation.handle(control, event)) return;
        switch (event.key) {
        case Qt.Key_Up:
            if (!firstInFocusChain) {
                let item = nextItemInFocusChain(false);
                if (item)
                    item.forceActiveFocus(Qt.TabFocusReason);
                if(!sendOutput)
                    event.accepted = true;
            }
            break;
        case Qt.Key_Down:
            if (!lastInFocusChain) {
                let item = nextItemInFocusChain();
                if (item)
                    item.forceActiveFocus(Qt.TabFocusReason);
                if(!sendOutput)
                    event.accepted = true;
            }
            break;
        }
    }
}
