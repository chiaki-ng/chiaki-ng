import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
import "controls" as C

C.ModalDialog {
    id: dialog
    property alias text: label.text
    property var callback
    property var rejectCallback
    property bool newDialogOpen: false
    property Item restoreFocusItem
    iconSource: C.Theme.icon("alert")
    onOpened: label.forceActiveFocus(Qt.TabFocusReason)
    onAccepted: {
        newDialogOpen = true;
        restoreFocus();
        callback();
    }
    onClosed: if(!newDialogOpen) { restoreFocus() }

    onRejected: {
        if(rejectCallback)
        {
            newDialogOpen = true;
            restoreFocus();
            rejectCallback();
        }
    }

    function restoreFocus() {
        if (restoreFocusItem)
            restoreFocusItem.forceActiveFocus(Qt.TabFocusReason);
        label.focus = false;
    }

    ColumnLayout {
        spacing: 28

        Label {
            id: label
            Layout.maximumWidth: Math.min(680, root.width - 160)
            wrapMode: Text.WordWrap
            color: C.Theme.textSecondary
            font.pixelSize: C.Theme.fontBody
            lineHeight: 1.15
            Keys.onEscapePressed: dialog.reject()
            Keys.onReturnPressed: dialog.accept()
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 12

            C.PillButton {
                text: qsTr("No")
                hint: root.controllerButton("moon")
                focusPolicy: Qt.NoFocus
                onClicked: dialog.reject()
            }

            C.PillButton {
                text: qsTr("Yes")
                kind: "primary"
                hint: root.controllerButton("cross")
                focusPolicy: Qt.NoFocus
                onClicked: dialog.accept()
            }
        }
    }
}
