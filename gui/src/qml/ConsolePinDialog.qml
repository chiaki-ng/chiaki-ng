import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

import org.streetpea.chiaking

import "controls" as C

DialogView {
    property var consoleIndex
    title: qsTr("Set console pin")
    buttonText: qsTr("Set")
    buttonEnabled: pin.acceptableInput
    onAccepted: {
        Chiaki.setConsolePin(consoleIndex, pin.text.trim());
        stack.pop();
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
                            text: qsTr("Remote Play PIN (4 digits):")
                        }

                        TextField {
                            id: pin
                            validator: RegularExpressionValidator { regularExpression: /[0-9]{4}/ }
                            echoMode: Chiaki.settings.streamerMode ? TextInput.Password : TextInput.Normal
                            Layout.preferredWidth: 400
                        }
        }

    }
}