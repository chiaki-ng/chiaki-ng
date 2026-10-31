import QtQuick
import QtQuick.Templates as T
import QtQuick.Controls.impl

// Rounded action button used across the redesigned UI.
T.Button {
    id: control

    // "primary" (accent gradient), "secondary" (raised surface), "ghost" (transparent), "danger"
    property string kind: "secondary"
    // Controller glyph shown before the label, e.g. root.controllerButton("cross")
    property url hint: ""
    property url iconSource: ""
    property bool compact: false
    // Lets a parent (e.g. the selected console card) promote the button visually
    property bool emphasized: false

    readonly property bool lit: visualFocus || emphasized
    readonly property color labelColor: {
        if (kind === "primary")
            return "#FFFFFF";
        if (kind === "danger")
            return Theme.danger;
        if (lit)
            return Theme.text;
        return hovered ? Theme.text : Theme.textSecondary;
    }

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    leftPadding: text ? (compact ? 16 : 22) : 12
    rightPadding: text ? (compact ? 18 : 24) : 12
    topPadding: 0
    bottomPadding: 0
    spacing: compact ? 8 : 10
    hoverEnabled: true

    font.family: Theme.fontFamily
    font.pixelSize: compact ? Theme.fontLabel : Theme.fontBody
    font.weight: Font.DemiBold

    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight
        opacity: control.enabled ? 1.0 : 0.5

        Row {
            id: row
            anchors.centerIn: parent
            spacing: control.spacing

            Image {
                anchors.verticalCenter: parent.verticalCenter
                visible: control.hint.toString() !== ""
                source: control.hint
                width: control.compact ? 22 : 26
                height: width
                sourceSize: Qt.size(width, height)
            }

            IconImage {
                anchors.verticalCenter: parent.verticalCenter
                visible: control.iconSource.toString() !== ""
                source: control.iconSource
                sourceSize: Qt.size(control.compact ? 18 : 20, control.compact ? 18 : 20)
                color: control.labelColor
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: text !== ""
                text: control.text
                font: control.font
                color: control.labelColor
            }
        }
    }

    background: Item {
        implicitWidth: control.compact ? 40 : 50
        implicitHeight: control.compact ? 40 : 50
        opacity: control.enabled ? 1.0 : 0.4

        Rectangle {
            id: focusRing
            anchors.fill: parent
            anchors.margins: -4
            radius: height / 2
            color: "transparent"
            border.width: 2
            border.color: Theme.accent
            opacity: control.visualFocus ? 1.0 : 0.0
            Behavior on opacity { NumberAnimation { duration: Theme.durFast } }
        }

        Rectangle {
            anchors.fill: parent
            radius: height / 2
            gradient: control.kind === "primary" ? primaryGradient : undefined
            color: {
                switch (control.kind) {
                case "danger":
                    return control.down ? "#40FB7185" : (control.hovered || control.lit ? "#33FB7185" : Theme.dangerSoft);
                case "ghost":
                    if (control.down)
                        return Theme.surfaceActive;
                    if (control.lit)
                        return Theme.accentSoft;
                    return control.hovered ? Theme.surfaceHover : "transparent";
                default:
                    if (control.down)
                        return Theme.surfaceActive;
                    if (control.lit)
                        return Theme.accentSoft;
                    return control.hovered ? Theme.surfaceHover : Theme.surfaceRaised;
                }
            }
            border.width: control.kind === "primary" ? 0 : 1
            border.color: control.lit && !control.visualFocus ? Theme.accent : (control.kind === "ghost" && !control.hovered ? "transparent" : Theme.stroke)

            Behavior on color { ColorAnimation { duration: Theme.durFast } }

            Gradient {
                id: primaryGradient
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Theme.accent }
                GradientStop { position: 1.0; color: Theme.accentAlt }
            }

            // Hover / press sheen for the gradient variant
            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                visible: control.kind === "primary"
                color: control.down ? "#26000000" : "#1FFFFFFF"
                opacity: control.down || control.hovered ? 1.0 : 0.0
                Behavior on opacity { NumberAnimation { duration: Theme.durFast } }
            }
        }
    }
}
