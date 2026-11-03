import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

import org.streetpea.chiaking

import "controls" as C

DialogView {
    title: qsTr("Add Manual Console")
    buttonText: qsTr("Add")
    buttonEnabled: hostField.text.trim() && (consoleCombo.model[consoleCombo.currentIndex].index != -1)
    onAccepted: {
        Chiaki.addManualHost(consoleCombo.model[consoleCombo.currentIndex].index, hostField.text.trim());
        close();
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
            rowSpacing: 20
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
                text: qsTr("Registered Consoles:")
            }

            C.ComboBox {
                id: consoleCombo
                Layout.preferredWidth: 400
                lastInFocusChain: true
                textRole: "name"
                model: {
                    let m = [];
                    if(Chiaki.settings.registeredHosts.length > 0)
                    {
                        m.push({
                            name: qsTr("Select an Option"),
                            index: -1,
                        });
                    }
                    let i = 0;
                    for (; i < Chiaki.settings.registeredHosts.length; ++i) {
                        let host = Chiaki.settings.registeredHosts[i];
                        m.push({
                            name: "%1 (%2)".arg(Chiaki.settings.streamerMode ? "hidden" : host.mac).arg(host.name),
                            index: i,
                        });
                    }
                    m.push({
                        name: qsTr("Register on first Connection"),
                        index: i,
                    });
                    return m;
                }
            }
        }
    }
}
