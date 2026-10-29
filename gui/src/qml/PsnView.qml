import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

import org.streetpea.chiaking

import "controls" as C

Rectangle {
    id: view
    property bool allowClose: false
    property bool cancelling: false
    property bool textVisible: true
    property bool registOnly: false
    property list<Item> restoreFocusItems
    color: "black"

    C.AppBackground {
        anchors.fill: parent
    }

    StackView.onActivated: infoLabel.visible = false

    function stop() {
        if (!allowClose)
            return;
        allowClose = false;
        cancelling = true;
        infoLabel.text = qsTr("Cancelling connection with console over PSN ...");
        Chiaki.psnCancel(false);
    }

    function grabInput(item) {
        Chiaki.window.grabInput();
        restoreFocusItems.push(Window.window.activeFocusItem);
        if (item)
            item.forceActiveFocus(Qt.TabFocusReason);
    }

    function releaseInput() {
        Chiaki.window.releaseInput();
        let item = restoreFocusItems.pop();
        if (item && item.visible)
            item.forceActiveFocus(Qt.TabFocusReason);
    }

    Keys.onEscapePressed: view.stop()

    Shortcut {
        sequence: "Ctrl+Q"
        onActivated: view.stop()
    }

    MouseArea {
        anchors.fill: parent
        enabled: view.allowClose
        acceptedButtons: Qt.RightButton
        onClicked: view.stop()
    }

    Label {
        id: infoLabel
        anchors.centerIn: parent
        font.pixelSize: C.Theme.fontHeadline
        font.weight: Font.Bold
        color: C.Theme.text
        opacity: textVisible ? 1.0: 0.0
        visible: opacity
        text: qsTr("Establishing connection with console over PSN ...")
        Behavior on opacity { NumberAnimation { duration: 250 } }
    }

    Item {
        anchors {
            top: parent.verticalCenter
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }

        BusyIndicator {
            id: spinner
            anchors.centerIn: parent
            width: 70
            height: width
        }

        Label {
            id: closeMessageLabel
            color: C.Theme.textSecondary
            font.pixelSize: C.Theme.fontLabel
            anchors {
                top: spinner.bottom
                horizontalCenter: spinner.horizontalCenter
                topMargin: 30
            }
            opacity: (textVisible && !cancelling) ? 1.0: 0.0
            visible: opacity
            text: {
                var typeString = registOnly ? qsTr("automatic registration") : qsTr("remote connection via PSN")
                qsTr("Press %1 to cancel %2").arg(Chiaki.controllers.length ? (root.controllerButton("circle").includes("deck") ? "B" : "Circle") : "escape or right-click").arg(typeString)
            }
        }

        Label {
            id: errorTitleLabel
            anchors {
                bottom: spinner.top
                horizontalCenter: spinner.horizontalCenter
            }
            font.pixelSize: 24
            visible: text
            onVisibleChanged: {
                if (visible) {
                    textVisible = false
                    view.allowClose = false
                    view.grabInput(errorTitleLabel)
                }
            }
            Keys.onReturnPressed: root.showMainView()
            Keys.onEscapePressed: root.showMainView()
        }

        Label {
            id: errorTextLabel
            anchors {
                top: errorTitleLabel.bottom
                horizontalCenter: errorTitleLabel.horizontalCenter
                topMargin: 10
            }
        }
    }

    Timer {
        id: closeTimer
        interval: 1500
        running: true
        onTriggered: {
            view.allowClose = true
            infoLabel.visible = infoLabel.opacity
        }
    }

    Timer {
        id: failTimer
        interval: 2000
        running: false
        onTriggered: root.showMainView()
    }

    Rectangle {
        anchors.fill: parent
        color: C.Theme.scrim
        opacity: sessionPinDialog.visible ? 1.0 : 0.0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: C.Theme.durMed } }
    }

    C.ModalDialog {
        id: sessionPinDialog
        title: qsTr("Console Login PIN")
        iconSource: C.Theme.icon("pin")
        closePolicy: Popup.NoAutoClose
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAboutToShow: {
            standardButton(Dialog.Ok).enabled = Qt.binding(function() {
                return pinField.acceptableInput;
            });
            textVisible = false
            view.grabInput(pinField);
        }
        onClosed: {
            textVisible = true
            view.releaseInput()
        }
        onAccepted: Chiaki.enterPin(pinField.text)
        onRejected: Chiaki.stopSession(false)

        TextField {
            id: pinField
            echoMode: Chiaki.settings.streamerMode ? TextInput.Password : TextInput.Normal
            implicitWidth: 320
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: C.Theme.fontTitle
            font.weight: Font.Bold
            font.letterSpacing: 12
            validator: RegularExpressionValidator { regularExpression: /[0-9]{4}/ }
            Keys.onReturnPressed: {
                if(sessionPinDialog.standardButton(Dialog.Ok).enabled)
                    sessionPinDialog.standardButton(Dialog.Ok).clicked()
            }
        }
    }

    Connections {
        target: Chiaki

        function onConnectStateChanged() 
        {
            switch(Chiaki.connectState)
            {
                case Chiaki.PsnConnectState.LinkingConsole:
                    infoLabel.text = registOnly ? qsTr("Registering PlayStation console with chiaki-ng ...") : qsTr("Linking chiaki-ng with PlayStation console ...")
                    view.allowClose = false
                    break
                case Chiaki.PsnConnectState.RegisteringConsole:
                    view.registOnly = true;
                    break
                case Chiaki.PsnConnectState.RegistrationFinished:
                    infoLabel.text = qsTr("Successfully registered console")
                    failTimer.restart()
                    break
                case Chiaki.PsnConnectState.DataConnectionStart:
                    infoLabel.text = qsTr("Console Linked ... Establishing data connection with console over PSN ...")
                    view.allowClose = true
                    break
                case Chiaki.PsnConnectState.DataConnectionFinished:
                    view.allowClose = false
                    break
                case Chiaki.PsnConnectState.ConnectFailed:
                    if(!cancelling)
                        infoLabel.text = qsTr("Connection over PSN failed closing ...")
                    failTimer.running = true
                    break
                case Chiaki.PsnConnectState.ConnectFailedStart:
                    if(!cancelling)
                        infoLabel.text = qsTr("PSN couldn't establish connection with PlayStation. Please try again ...")
                    failTimer.running = true
                    break
                case Chiaki.PsnConnectState.ConnectFailedConsoleUnreachable:
                    if(!cancelling)
                        infoLabel.text = qsTr("Couldn't contact PlayStation over established connection, likely unsupported network type")
                    failTimer.running = true
                    break
                case Chiaki.PsnConnectState.WaitingForInternet:
                    infoLabel.text = qsTr("Establishing Internet Connection to PSN...")
                    break
            }
        }

        function onSessionChanged() 
        {
            if (!Chiaki.session) {
                if (errorTitleLabel.text)
                    failTimer.start();
                else if(!failTimer.running)
                    root.showMainView();
            }
        }

        function onSessionError(title, text)
        {
            errorTitleLabel.text = title;
            errorTextLabel.text = text;
            closeTimer.start();
        }

        function onSessionPinDialogRequested() 
        {
            if (sessionPinDialog.opened)
                return;
            sessionPinDialog.open();
        }
    }
}
