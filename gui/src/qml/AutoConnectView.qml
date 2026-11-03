import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

import org.streetpea.chiaking

import "controls" as C

Rectangle {
    id: view
    property bool allowClose: false
    property bool textVisible: false
    color: "black"

    C.AppBackground {
        anchors.fill: parent
    }

    function stop() {
        if (!allowClose)
            return;
        Chiaki.stopAutoConnect();
        root.showMainView();
    }

    function cancel() {
        if(!allowClose)
            return;
        view.textVisible = false;
        infoLabel.text = qsTr("Cancelling connection...");
        failTimer.start();
    }

    Keys.onEscapePressed: view.cancel()

    Shortcut {
        sequence: "Ctrl+Q"
        onActivated: view.cancel()
    }

    MouseArea {
        anchors.fill: parent
        enabled: view.allowClose
        acceptedButtons: Qt.RightButton
        onClicked: view.cancel()
    }

    Label {
        id: infoLabel
        anchors.centerIn: parent
        font.pixelSize: C.Theme.fontHeadline
        font.weight: Font.Bold
        color: C.Theme.text
        opacity: view.allowClose ? 1.0 : 0.0
        visible: opacity
        text: qsTr("Waiting for console...")

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
            running: view.visible
        }
        Label {
            id: closeMessageLabel
            color: C.Theme.textSecondary
            font.pixelSize: C.Theme.fontLabel
            anchors {
                topMargin: 30
                top: spinner.bottom
                horizontalCenter: spinner.horizontalCenter
            }
            opacity: textVisible ? 1.0: 0.0
            visible: opacity
            text: qsTr("Press %1 to cancel connection").arg(Chiaki.controllers.length ? (root.controllerButton("circle").includes("deck") ? "B" : "Circle") : "escape or right-click")
        }
    }

    Timer {
        interval: 1500
        running: true
        onTriggered: {
            view.allowClose = true
            view.textVisible = true
        }
    }

    Timer {
        id: failTimer
        interval: 2000
        running: false
        onTriggered: view.stop();
    }

    Connections {
        target: Chiaki

        function onWakeupStartFailed() {
            view.textVisible = false;
            infoLabel.text = qsTr("Timed out waiting for console. Exiting...");
            failTimer.start();
        }
    }
}
