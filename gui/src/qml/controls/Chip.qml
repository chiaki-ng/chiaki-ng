import QtQuick

// Small status pill: optional colored dot + label.
Rectangle {
    id: chip

    property alias text: label.text
    property color tone: Theme.textSecondary
    property bool dot: false
    property bool tinted: false

    implicitWidth: row.implicitWidth + 22
    implicitHeight: 28
    radius: height / 2
    color: tinted ? Qt.rgba(tone.r, tone.g, tone.b, 0.14) : Theme.surfaceRaised
    border.width: 1
    border.color: tinted ? Qt.rgba(tone.r, tone.g, tone.b, 0.28) : Theme.stroke

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 7

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            visible: chip.dot
            width: 8
            height: 8
            radius: 4
            color: chip.tone
        }

        Text {
            id: label
            anchors.verticalCenter: parent.verticalCenter
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontCaption
            font.weight: Font.DemiBold
            color: chip.tinted ? chip.tone : Theme.textSecondary
        }
    }
}
