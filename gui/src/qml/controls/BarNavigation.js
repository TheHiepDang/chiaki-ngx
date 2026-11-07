// Directional navigation is scoped to the horizontal live settings strip.
function controls(scope) {
    const result = [];
    function visit(item) {
        if (!item.visible || !item.enabled) return;
        if (item.navigationScope === scope) result.push(item);
        for (let n = 0; n < item.children.length; ++n) visit(item.children[n]);
    }
    visit(scope);
    // The row is declared in visual order; avoid stale coordinates while a toggle relayouts it.
    return result;
}

function handle(control, event) {
    const scope = control.navigationScope;
    if (!scope || (control.popup && control.popup.visible)) return false;
    const slider = control.barEditing !== undefined;
    if (slider && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space)) {
        if (!event.isAutoRepeat) control.barEditing = !control.barEditing;
    } else if (slider && event.key === Qt.Key_Escape && control.barEditing) {
        control.barEditing = false;
    } else if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
        if (slider && control.barEditing) return false;
        const items = controls(scope);
        const next = items.indexOf(control) + (event.key === Qt.Key_Right ? 1 : -1);
        if (next >= 0 && next < items.length) items[next].forceActiveFocus(Qt.TabFocusReason);
    } else if (event.key === Qt.Key_Up) {
        if (scope.previousFocusItem) scope.previousFocusItem.forceActiveFocus(Qt.TabFocusReason);
    } else if (event.key === Qt.Key_Down) {
        // The strip is one horizontal row: Down must never mean "move right".
    } else {
        return false;
    }
    event.accepted = true;
    return true;
}
