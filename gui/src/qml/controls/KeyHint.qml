import QtQuick

// Controller/keyboard hint: glyph image followed by a short label.
Row {
    id: hint

    property url glyph: ""
    property string keyText: ""
    property alias text: label.text

    spacing: 8

    Image {
        anchors.verticalCenter: parent.verticalCenter
        visible: hint.glyph.toString() !== ""
        source: hint.glyph
        width: 24
        height: 24
        sourceSize: Qt.size(width, height)
    }

    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        visible: hint.keyText !== ""
        width: Math.max(26, keyLabel.implicitWidth + 12)
        height: 24
        radius: 6
        color: Theme.surfaceRaised
        border.width: 1
        border.color: Theme.strokeStrong

        Text {
            id: keyLabel
            anchors.centerIn: parent
            text: hint.keyText
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontMicro
            font.weight: Font.Bold
            color: Theme.textSecondary
        }
    }

    Text {
        id: label
        anchors.verticalCenter: parent.verticalCenter
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontCaption
        font.weight: Font.Medium
        color: Theme.textSecondary
    }
}
