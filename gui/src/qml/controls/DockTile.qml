import QtQuick
import QtQuick.Templates as T
import QtQuick.Controls.impl

// Icon + caption tile used in the in-stream dock.
// `selected` is bound to backend state (not `checked`) so clicks never break the binding.
T.Button {
    id: control

    property url iconSource: ""
    property bool selected: false
    // Optional 1-4 quality bars shown instead of an icon
    property int level: 0

    readonly property color tint: selected ? Theme.accentBright : (hovered || activeFocus ? Theme.text : Theme.textSecondary)

    implicitWidth: Math.max(78, caption.implicitWidth + 22)
    implicitHeight: 84
    padding: 0
    hoverEnabled: true

    contentItem: Item {
        Column {
            anchors.centerIn: parent
            spacing: 8

            Item {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 26
                height: 26

                IconImage {
                    anchors.centerIn: parent
                    visible: control.level === 0
                    source: control.iconSource
                    sourceSize: Qt.size(24, 24)
                    color: control.tint
                }

                Row {
                    anchors.centerIn: parent
                    visible: control.level > 0
                    spacing: 3

                    Repeater {
                        model: 4
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: 4
                            height: 8 + index * 4
                            radius: 2
                            color: index < control.level ? control.tint : Theme.strokeStrong
                        }
                    }
                }
            }

            Text {
                id: caption
                anchors.horizontalCenter: parent.horizontalCenter
                text: control.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMicro + 1
                font.weight: Font.DemiBold
                color: control.tint
            }
        }
    }

    background: Rectangle {
        radius: 18
        opacity: control.enabled ? 1.0 : 0.4
        color: {
            if (control.down)
                return Theme.surfaceActive;
            if (control.selected)
                return Theme.accentSoft;
            if (control.hovered || control.activeFocus)
                return Theme.surfaceHover;
            return "transparent";
        }
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? Theme.accent : (control.selected ? "#664D8DFF" : "transparent")
    }
}
