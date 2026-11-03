import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
import "controls" as C

import org.streetpea.chiaking

C.ModalDialog {
    id: dialog
    property alias text: label.text
    property var remotePlay
    property var callback
    property bool newDialogOpen: false
    property Item restoreFocusItem
    iconSource: C.Theme.icon(remotePlay ? "globe" : "gamepad")
    onOpened: label.forceActiveFocus(Qt.TabFocusReason)
    onAccepted: {
        newDialogOpen = true;
        restoreFocus();
        callback();
    }
    onRejected: {
        if(dialog.remotePlay)
            Chiaki.settings.remotePlayAsk = false;
        else
            Chiaki.settings.addSteamShortcutAsk = false;
    }
    onClosed: {
        if(newDialogOpen)
            return;
        restoreFocus();
        if(!remotePlay && Chiaki.settings.remotePlayAsk)
        {
            if(!Chiaki.settings.psnRefreshToken || !Chiaki.settings.psnAuthToken || !Chiaki.settings.psnAuthTokenExpiry || !Chiaki.settings.psnAccountId)
                root.showRemindDialog(qsTr("Remote Play via PSN"), qsTr("Would you like to connect to PSN?\nThis enables:\n- Automatic registration\n- Playing outside of your home network without port forwarding?") + "\n\n" + qsTr("(Note: If you select no now and want to do this later, go to the Config section of the settings.)"), true, () => root.showPSNTokenDialog(false));
            else
                Chiaki.settings.remotePlayAsk = false;
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
            Layout.maximumWidth: Math.min(720, root.width - 160)
            wrapMode: Text.WordWrap
            color: C.Theme.textSecondary
            font.pixelSize: C.Theme.fontBody
            lineHeight: 1.15
            Keys.onEscapePressed: dialog.reject()
            Keys.onReturnPressed: dialog.accept()
            Keys.onYesPressed: dialog.close()
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 12

            C.PillButton {
                text: qsTr("Remind Me Later")
                kind: "ghost"
                hint: root.controllerButton("pyramid")
                focusPolicy: Qt.NoFocus
                onClicked: dialog.close()
            }

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
