import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Window

import org.streetpea.chiaking

Window {
    id: streamMenuWindow

    property bool open: false
    property bool closing: false

    signal closeRequested()
    signal displaySettingsRequested()
    signal placeboSettingsRequested()
    signal mainViewRequested()
    signal closeAnimationFinished()

    visible: open || closing
    flags: Qt.Tool | Qt.FramelessWindowHint
    color: "transparent"
    modality: Qt.NonModal

    function focusInitialItem() {
        requestActivate();
        menuBar.initialFocusItem.forceActiveFocus(Qt.TabFocusReason);
    }

    onVisibleChanged: {
        if (visible)
            focusTimer.restart();
    }

    onActiveChanged: {
        if (active && visible)
            focusTimer.restart();
    }

    Shortcut {
        sequence: StandardKey.Cancel
        onActivated: streamMenuWindow.closeRequested()
    }

    Shortcut {
        sequence: "Ctrl+O"
        onActivated: {
            if (streamMenuWindow.open)
                streamMenuWindow.closeRequested();
        }
    }

    Behavior on y {
        NumberAnimation {
            id: menuAnimation
            duration: 250
            onRunningChanged: {
                if (!running && !streamMenuWindow.open)
                    streamMenuWindow.closeAnimationFinished();
            }
        }
    }

    Timer {
        id: focusTimer
        interval: 0
        repeat: false
        onTriggered: {
            if (streamMenuWindow.visible)
                streamMenuWindow.focusInitialItem();
        }
    }

    FocusScope {
        anchors.fill: parent
        focus: true
        Keys.onMenuPressed: streamMenuWindow.closeRequested()
        Keys.onEscapePressed: streamMenuWindow.closeRequested()

        StreamMenuBar {
            id: menuBar
            anchors.fill: parent
            opaque: true
            active: streamMenuWindow.visible
            onCloseRequested: streamMenuWindow.closeRequested()
            onMainViewRequested: streamMenuWindow.mainViewRequested()
            onDisplaySettingsRequested: streamMenuWindow.displaySettingsRequested()
            onPlaceboSettingsRequested: streamMenuWindow.placeboSettingsRequested()
        }
    }
}
