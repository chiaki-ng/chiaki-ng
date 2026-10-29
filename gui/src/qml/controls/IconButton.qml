import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import QtQuick.Controls.impl

// Circular icon-only button (back, close, settings, ...).
T.Button {
    id: control

    property url iconSource: ""
    property int size: 48
    property int iconSize: Math.round(size * 0.46)
    property bool filled: true
    property color iconColor: hovered || visualFocus ? Theme.text : Theme.textSecondary

    implicitWidth: size
    implicitHeight: size
    padding: 0
    hoverEnabled: true

    contentItem: Item {
        IconImage {
            anchors.centerIn: parent
            source: control.iconSource
            sourceSize: Qt.size(control.iconSize, control.iconSize)
            color: control.iconColor
        }
    }

    background: Rectangle {
        radius: width / 2
        color: {
            if (control.down)
                return Theme.surfaceActive;
            if (control.hovered || control.visualFocus)
                return Theme.surfaceHover;
            return control.filled ? Theme.surfaceRaised : "transparent";
        }
        border.width: 1
        border.color: control.visualFocus ? Theme.accent : (control.filled || control.hovered ? Theme.stroke : "transparent")
        Behavior on color { ColorAnimation { duration: Theme.durFast } }
    }

    ToolTip.visible: hovered && ToolTip.text
    ToolTip.delay: 600
}
