import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

// Shared look for modal popups: elevated card, icon badge header, dimmed backdrop.
Dialog {
    id: dialog

    property url iconSource: Theme.icon("alert")
    property color tone: Theme.accent

    parent: Overlay.overlay
    width: Math.min(implicitWidth, root.width - 64)
    x: Math.round((root.width - width) / 2)
    y: Math.round((root.height - height) / 2)
    modal: true
    // The backdrop is drawn by Main.qml (modalScrim): the Overlay of this offscreen window isn't sized reliably
    dim: false
    padding: 32
    topPadding: 8
    bottomPadding: 28

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: Theme.durMed; easing.type: Easing.OutCubic }
        NumberAnimation { property: "scale"; from: 0.94; to: 1.0; duration: Theme.durSlow; easing.type: Easing.OutBack; easing.overshoot: 1.2 }
    }

    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1.0; to: 0.0; duration: Theme.durFast }
        NumberAnimation { property: "scale"; from: 1.0; to: 0.97; duration: Theme.durFast }
    }

    background: Item {
        // Read by the base Material Dialog's default header
        property real radius: Theme.radiusXL

        // Cheap layered shadow instead of a blur effect
        Rectangle {
            anchors.fill: parent
            anchors.margins: -18
            anchors.topMargin: -6
            radius: Theme.radiusXL + 18
            color: "#33000000"
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: -6
            radius: Theme.radiusXL + 6
            color: "#40000000"
        }
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusXL
            color: Theme.surface
            border.width: 1
            border.color: Theme.strokeStrong

            Rectangle {
                anchors {
                    top: parent.top
                    left: parent.left
                    right: parent.right
                    margins: 1
                }
                height: parent.height * 0.5
                radius: Theme.radiusXL
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.rgba(dialog.tone.r, dialog.tone.g, dialog.tone.b, 0.10) }
                    GradientStop { position: 1.0; color: "transparent" }
                }
            }
        }
    }

    footer: DialogButtonBox {
        visible: count > 0
        alignment: Qt.AlignRight
        spacing: 12
        leftPadding: 32
        rightPadding: 32
        topPadding: 0
        bottomPadding: 28
        background: null
        delegate: PillButton {
            kind: DialogButtonBox.buttonRole === DialogButtonBox.AcceptRole ? "primary" : "secondary"
            emphasized: activeFocus
        }
    }

    header: Item {
        implicitHeight: headerRow.implicitHeight + 56
        implicitWidth: headerRow.implicitWidth + 64

        RowLayout {
            id: headerRow
            anchors {
                left: parent.left
                right: parent.right
                bottom: parent.bottom
                leftMargin: 32
                rightMargin: 32
                bottomMargin: 18
            }
            spacing: 16

            Rectangle {
                Layout.preferredWidth: 48
                Layout.preferredHeight: 48
                radius: 16
                color: Qt.rgba(dialog.tone.r, dialog.tone.g, dialog.tone.b, 0.16)
                border.width: 1
                border.color: Qt.rgba(dialog.tone.r, dialog.tone.g, dialog.tone.b, 0.35)
                visible: dialog.iconSource.toString() !== ""

                Image {
                    anchors.centerIn: parent
                    source: dialog.iconSource
                    sourceSize: Qt.size(24, 24)
                }
            }

            Text {
                Layout.fillWidth: true
                text: dialog.title
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontHeadline
                font.weight: Font.Bold
                color: Theme.text
                elide: Text.ElideRight
            }
        }
    }
}
