import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import "BarNavigation.js" as BarNavigation

CheckBox {
    id: control
    property Item navigationScope: null
    property bool firstInFocusChain: false
    property bool lastInFocusChain: false
    property bool sendOutput: false

    Keys.onReleased: event => {
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) event.accepted = true;
    }

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
        case Qt.Key_Enter:
        case Qt.Key_Return:
            if (activeFocus && !event.isAutoRepeat) {
                toggle();
                toggled();
                clicked();
            }
            event.accepted = true;
            break;
        }
    }
}
