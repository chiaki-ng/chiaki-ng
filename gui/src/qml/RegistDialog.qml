import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

import org.streetpea.chiaking

import "controls" as C

DialogView {
    property bool ps5: true
    property alias host: hostField.text
    title: qsTr("Register Console")
    buttonText: qsTr("Register")
    buttonEnabled: hostField.text.trim() && pin.acceptableInput && cpin.acceptableInput && (!onlineId.visible || onlineId.text.trim()) && (!accountId.visible || accountId.text.trim())
    StackView.onActivated: {
        if(Chiaki.settings.psnAccountId)
            accountId.text = Chiaki.settings.psnAccountId
    }
    onAccepted: {
        let psnId = onlineId.visible ? onlineId.text.trim() : accountId.text.trim();
        let registerOk = Chiaki.registerHost(hostField.text.trim(), psnId, pin.text.trim(), cpin.text.trim(), hostField.text.trim() == "255.255.255.255", consoleButtons.checkedButton.target, function(msg, ok, done) {
            if (!done)
                logArea.text += msg + "\n";
            else
                logDialog.standardButtons = Dialog.Close;
        });
        if (registerOk) {
            logArea.text = "";
            logDialog.open();
        }
    }

    Item {
        Rectangle {
            anchors.fill: formGrid
            anchors.margins: -36
            radius: C.Theme.radiusXL
            color: C.Theme.surface
            border.width: 1
            border.color: C.Theme.stroke
        }

        GridLayout {
            id: formGrid
            anchors {
                top: parent.top
                horizontalCenter: parent.horizontalCenter
                topMargin: 64
            }
            columns: 2
            rowSpacing: 10
            columnSpacing: 20

            Label {
                Layout.alignment: Qt.AlignRight
                text: qsTr("Host:")
            }

            C.TextField {
                id: hostField
                echoMode: Chiaki.settings.streamerMode ? TextInput.Password : TextInput.Normal
                Layout.preferredWidth: 400
                firstInFocusChain: true
            }

            Label {
                Layout.alignment: Qt.AlignRight
                text: qsTr("PSN Online-ID:")
                visible: onlineId.visible
            }

            C.TextField {
                id: onlineId
                echoMode: Chiaki.settings.streamerMode ? TextInput.Password : TextInput.Normal
                visible: ps4_7.checked
                placeholderText: qsTr("username, case-sensitive")
                Layout.preferredWidth: 400
            }

            Label {
                Layout.alignment: Qt.AlignRight
                text: qsTr("PSN Account-ID:")
                visible: accountId.visible
            }

            C.TextField {
                id: accountId
                visible: !ps4_7.checked
                echoMode: Chiaki.settings.streamerMode ? TextInput.Password : TextInput.Normal
                placeholderText: qsTr("base64")
                Layout.preferredWidth: 400 - loginButton.width - 10
                KeyNavigation.priority: {
                    if(readOnly)
                        KeyNavigation.BeforeItem
                    else
                        KeyNavigation.AfterItem
                }
                KeyNavigation.up: hostField
                KeyNavigation.right: loginButton
                KeyNavigation.down: pin

                C.Button {
                    id: loginButton
                    anchors {
                        left: parent.right
                        verticalCenter: parent.verticalCenter
                        leftMargin: 10
                    }
                    topPadding: 18
                    bottomPadding: 18
                    text: qsTr("PSN Login")
                    onClicked: stack.push(psnLoginDialogComponent, {login: true, callback: (id) => accountId.text = id})
                    visible: !Chiaki.settings.psnAccountId
                    Material.roundedScale: Material.SmallScale
                    KeyNavigation.priority: KeyNavigation.BeforeItem
                    KeyNavigation.up: hostField
                    KeyNavigation.left: accountId
                    KeyNavigation.right: lookupButton
                    KeyNavigation.down: pin
                }
                C.Button {
                    id: lookupButton
                    anchors {
                        left: loginButton.right
                        verticalCenter: parent.verticalCenter
                        leftMargin: 10
                    }
                    topPadding: 18
                    bottomPadding: 18
                    text: qsTr("Public Lookup")
                    onClicked: stack.push(psnLoginDialogComponent, {login: false, callback: (id) => accountId.text = id})
                    visible: !Chiaki.settings.psnAccountId
                    Material.roundedScale: Material.SmallScale
                    KeyNavigation.up: hostField
                    KeyNavigation.priority: KeyNavigation.BeforeItem
                    KeyNavigation.left: loginButton
                    KeyNavigation.right: lookupButton
                    KeyNavigation.down: pin
                }
            }

            Label {
                Layout.alignment: Qt.AlignRight
                text: qsTr("Remote Play PIN:")
            }

            C.TextField {
                id: pin
                validator: RegularExpressionValidator { regularExpression: /[0-9]{8}/ }
                Layout.preferredWidth: 400
                echoMode: Chiaki.settings.streamerMode ? TextInput.Password : TextInput.Normal
                KeyNavigation.priority: {
                    if(readOnly)
                        KeyNavigation.BeforeItem
                    else
                        KeyNavigation.AfterItem
                }
                KeyNavigation.up: accountId
            }

            Label {
                Layout.alignment: Qt.AlignRight
                text: qsTr("Console Pin [Optional]")
            }

            C.TextField {
                id: cpin
                echoMode: Chiaki.settings.streamerMode ? TextInput.Password : TextInput.Normal
                validator: RegularExpressionValidator { regularExpression: /^$|[0-9]{4}/ }
                Layout.preferredWidth: 400
            }

            Label {
                Layout.alignment: Qt.AlignRight
                text: qsTr("Console:")
            }

            ColumnLayout {
                spacing: 0

                C.RadioButton {
                    id: ps4_7
                    property int target: 800
                    text: qsTr("PS4 Firmware < 7.0")
                }

                C.RadioButton {
                    id: ps4_75
                    property int target: 900
                    text: qsTr("PS4 Firmware >= 7.0, < 8.0")
                }

                C.RadioButton {
                    id: ps4_8
                    property int target: 1000
                    text: qsTr("PS4 Firmware >= 8.0")
                    checked: !ps5
                }

                C.RadioButton {
                    id: ps5_0
                    property int target: 1000100
                    lastInFocusChain: true
                    text: qsTr("PS5")
                    checked: ps5
                }
            }
        }

        ButtonGroup {
            id: consoleButtons
            buttons: [ps4_7, ps4_75, ps4_8, ps5_0]
        }

        Item {
            Dialog {
                id: logDialog
                parent: Overlay.overlay
                x: Math.round((root.width - width) / 2)
                y: Math.round((root.height - height) / 2)
                title: qsTr("Register Console")
                modal: true
                closePolicy: Popup.NoAutoClose
                standardButtons: Dialog.Cancel
                Material.roundedScale: Material.MediumScale
                onOpened: logArea.forceActiveFocus(Qt.TabFocusReason)
                onClosed: stack.pop();

                Flickable {
                    id: logFlick
                    implicitWidth: 600
                    implicitHeight: 400
                    clip: true
                    contentWidth: logArea.contentWidth
                    contentHeight: logArea.contentHeight
                    flickableDirection: Flickable.AutoFlickIfNeeded
                    ScrollBar.vertical: ScrollBar {
                        id: logScrollbar
                        policy: ScrollBar.AlwaysOn
                        visible: logFlick.contentHeight > logFlick.implicitHeight
                    }

                    Label {
                        id: logArea
                        width: logFlick.width
                        wrapMode: TextEdit.Wrap
                        Keys.onReturnPressed: if (logDialog.standardButtons == Dialog.Close) logDialog.close()
                        Keys.onEscapePressed: logDialog.close()
                        Keys.onPressed: (event) => {
                            switch (event.key) {
                            case Qt.Key_Up:
                                if(logScrollbar.position > 0.001)
                                    logFlick.flick(0, 500);
                                event.accepted = true;
                                break;
                            case Qt.Key_Down:
                                if(logScrollbar.position < 1.0 - logScrollbar.size - 0.001)
                                    logFlick.flick(0, -500);
                                event.accepted = true;
                                break;
                            }
                        }
                    }
                }
            }
        }

        Component {
            id: psnLoginDialogComponent
            PSNLoginDialog { }
        }
    }
}
