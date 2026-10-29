import QtQuick
import QtQuick.Controls

// Segmented tab bar with a sliding accent pill behind the current tab.
TabBar {
    id: control

    spacing: 4
    padding: 6

    background: Rectangle {
        radius: Theme.radiusL
        color: Theme.surface
        border.width: 1
        border.color: Theme.stroke
    }

    contentItem: ListView {
        clip: true
        model: control.contentModel
        currentIndex: control.currentIndex
        spacing: control.spacing
        orientation: ListView.Horizontal
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.AutoFlickIfNeeded
        snapMode: ListView.SnapToItem
        highlightMoveDuration: Theme.durMed
        highlightMoveVelocity: -1
        highlightResizeDuration: Theme.durMed
        highlightResizeVelocity: -1
        highlightFollowsCurrentItem: true
        highlightRangeMode: ListView.ApplyRange
        preferredHighlightBegin: 48
        preferredHighlightEnd: width - 48

        highlight: Rectangle {
            radius: Theme.radiusM
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Theme.accent }
                GradientStop { position: 1.0; color: Theme.accentAlt }
            }
        }
    }
}
