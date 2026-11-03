import QtQuick
import QtQuick.Controls

TabButton {
    id: control

    // Equal widths when everything fits; natural widths (and a scrollable bar) when it doesn't
    width: TabBar.tabBar ? Math.max(implicitWidth, (TabBar.tabBar.availableWidth - (TabBar.tabBar.count - 1) * TabBar.tabBar.spacing) / TabBar.tabBar.count) : implicitWidth
    leftPadding: 18
    rightPadding: 18
    hoverEnabled: true
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontLabel
    font.weight: Font.DemiBold

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.checked ? "#FFFFFF" : (control.hovered ? Theme.text : Theme.textSecondary)
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        Behavior on color { ColorAnimation { duration: Theme.durFast } }
    }

    background: Rectangle {
        implicitHeight: 46
        radius: Theme.radiusM
        color: !control.checked && control.hovered ? Theme.surfaceHover : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.durFast } }
    }
}
