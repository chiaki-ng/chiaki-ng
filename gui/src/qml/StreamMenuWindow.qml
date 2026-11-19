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
        closeButton.forceActiveFocus(Qt.TabFocusReason);
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

        Rectangle {
            anchors.fill: parent
            color: "#2b2b2b"
        }

        RowLayout {
            anchors {
                left: parent.left
                bottom: parent.bottom
                leftMargin: 30
                bottomMargin: 40
            }
            spacing: 0

            ToolButton {
                id: closeButton
                Layout.rightMargin: 20
                text: "×"
                padding: 10
                font.pixelSize: 50
                down: activeFocus
                activeFocusOnTab: true
                onClicked: {
                    if (Chiaki.session)
                        Chiaki.window.close();
                    else
                        streamMenuWindow.mainViewRequested();
                }
                KeyNavigation.right: volumeSlider
                Keys.onReturnPressed: clicked()
                Keys.onEscapePressed: streamMenuWindow.closeRequested()
            }

            ToolSeparator {
                Layout.leftMargin: -10
                Layout.rightMargin: 10
            }

            Slider {
                id: volumeSlider
                Layout.rightMargin: 20
                orientation: Qt.Vertical
                from: 0
                to: 128
                Layout.preferredHeight: 100
                padding: 10
                stepSize: 1
                value: Chiaki.settings.audioVolume
                onMoved: Chiaki.settings.audioVolume = value
                KeyNavigation.left: closeButton
                KeyNavigation.right: muteButton
                Keys.onEscapePressed: streamMenuWindow.closeRequested()

                Label {
                    anchors {
                        top: parent.bottom
                        horizontalCenter: parent.horizontalCenter
                        leftMargin: 10
                    }
                    text: ((parent.value / 128.0) * 100).toFixed(0) + qsTr("% Volume")
                }
            }

            ToolSeparator {
                Layout.leftMargin: -10
                Layout.rightMargin: -10
            }

            ToolButton {
                id: muteButton
                Layout.rightMargin: 20
                text: qsTr("Mic")
                padding: 10
                checkable: true
                enabled: Chiaki.session && Chiaki.session.connected && !Chiaki.session.viewerMode
                checked: Chiaki.session && !Chiaki.session.muted
                onToggled: Chiaki.session.muted = !Chiaki.session.muted
                KeyNavigation.left: volumeSlider
                KeyNavigation.right: viewerModeButton
                Keys.onReturnPressed: toggled()
                Keys.onEscapePressed: streamMenuWindow.closeRequested()
            }

            ToolButton {
                id: viewerModeButton
                Layout.rightMargin: 20
                text: qsTr("View Only")
                padding: 10
                checkable: true
                checked: Chiaki.session && Chiaki.session.viewerMode
                onToggled: if (Chiaki.session) Chiaki.session.viewerMode = checked
                KeyNavigation.left: muteButton
                KeyNavigation.right: zoomButton
                Keys.onReturnPressed: toggled()
                Keys.onEscapePressed: streamMenuWindow.closeRequested()
            }

            ToolButton {
                id: zoomButton
                text: qsTr("Zoom")
                padding: 10
                checkable: true
                checked: Chiaki.window.videoMode == ChiakiWindow.VideoMode.Zoom
                onToggled: Chiaki.window.videoMode = Chiaki.window.videoMode == ChiakiWindow.VideoMode.Zoom ? ChiakiWindow.VideoMode.Normal : ChiakiWindow.VideoMode.Zoom
                KeyNavigation.left: viewerModeButton
                KeyNavigation.right: Chiaki.window.videoMode == ChiakiWindow.VideoMode.Zoom ? zoomFactor : stretchButton
                Keys.onReturnPressed: toggled()
                Keys.onEscapePressed: streamMenuWindow.closeRequested()
            }

            Slider {
                id: zoomFactor
                orientation: Qt.Vertical
                from: -1
                to: 4
                Layout.preferredHeight: 100
                stepSize: 0.01
                visible: Chiaki.window.videoMode == ChiakiWindow.VideoMode.Zoom
                value: Chiaki.window.ZoomFactor
                onMoved: {
                    Chiaki.window.ZoomFactor = value
                    Chiaki.settings.sZoomFactor = value
                }

                Label {
                    anchors {
                        top: parent.bottom
                        horizontalCenter: parent.horizontalCenter
                        leftMargin: 10
                    }
                    text: {
                        if (parent.value === -1)
                            return qsTr("No Black Bars");
                        if (parent.value >= 0)
                            return qsTr((parent.value + 1).toFixed(2)) + qsTr(" x");
                        return qsTr(parent.value.toFixed(2)) + qsTr(" x");
                    }
                }
            }

            ToolSeparator {
                Layout.leftMargin: -10
                Layout.rightMargin: -10
            }

            ToolButton {
                id: stretchButton
                Layout.rightMargin: 50
                text: qsTr("Stretch")
                padding: 10
                checkable: true
                checked: Chiaki.window.videoMode == ChiakiWindow.VideoMode.Stretch
                onToggled: Chiaki.window.videoMode = Chiaki.window.videoMode == ChiakiWindow.VideoMode.Stretch ? ChiakiWindow.VideoMode.Normal : ChiakiWindow.VideoMode.Stretch
                KeyNavigation.left: Chiaki.window.videoMode == ChiakiWindow.VideoMode.Zoom ? zoomFactor : zoomButton
                KeyNavigation.right: defaultButton
                Keys.onReturnPressed: toggled()
                Keys.onEscapePressed: streamMenuWindow.closeRequested()
            }

            ToolButton {
                id: defaultButton
                text: qsTr("Default")
                padding: 10
                checkable: true
                checked: Chiaki.window.videoPreset == ChiakiWindow.VideoPreset.Default
                onToggled: {
                    Chiaki.window.videoPreset = ChiakiWindow.VideoPreset.Default
                    Chiaki.settings.videoPreset = ChiakiWindow.VideoPreset.Default
                }
                KeyNavigation.left: stretchButton
                KeyNavigation.right: highQualityButton
                Keys.onReturnPressed: toggled()
                Keys.onEscapePressed: streamMenuWindow.closeRequested()
            }

            ToolSeparator {
                Layout.leftMargin: -10
                Layout.rightMargin: -10
            }

            ToolButton {
                id: highQualityButton
                text: qsTr("High Quality")
                padding: 10
                checkable: true
                checked: Chiaki.window.videoPreset == ChiakiWindow.VideoPreset.HighQuality
                onToggled: {
                    Chiaki.window.videoPreset = ChiakiWindow.VideoPreset.HighQuality
                    Chiaki.settings.videoPreset = ChiakiWindow.VideoPreset.HighQuality
                }
                KeyNavigation.left: defaultButton
                KeyNavigation.right: highQualitySpatialButton
                Keys.onReturnPressed: toggled()
                Keys.onEscapePressed: streamMenuWindow.closeRequested()
            }

            ToolButton {
                id: highQualitySpatialButton
                text: qsTr("HQ + Spatial")
                padding: 10
                checkable: true
                checked: Chiaki.window.videoPreset == ChiakiWindow.VideoPreset.HighQualitySpatial
                onToggled: {
                    Chiaki.window.videoPreset = ChiakiWindow.VideoPreset.HighQualitySpatial
                    Chiaki.settings.videoPreset = ChiakiWindow.VideoPreset.HighQualitySpatial
                }
                KeyNavigation.left: highQualityButton
                KeyNavigation.right: highQualityAdvancedSpatialButton
                Keys.onReturnPressed: toggled()
                Keys.onEscapePressed: streamMenuWindow.closeRequested()
            }

            ToolButton {
                id: highQualityAdvancedSpatialButton
                text: qsTr("HQ + Adv Spatial")
                padding: 10
                checkable: true
                checked: Chiaki.window.videoPreset == ChiakiWindow.VideoPreset.HighQualityAdvancedSpatial
                onToggled: {
                    Chiaki.window.videoPreset = ChiakiWindow.VideoPreset.HighQualityAdvancedSpatial
                    Chiaki.settings.videoPreset = ChiakiWindow.VideoPreset.HighQualityAdvancedSpatial
                }
                KeyNavigation.left: highQualitySpatialButton
                KeyNavigation.right: customButton
                Keys.onReturnPressed: toggled()
                Keys.onEscapePressed: streamMenuWindow.closeRequested()
            }

            ToolSeparator {
                Layout.leftMargin: -10
                Layout.rightMargin: -10
            }

            ToolButton {
                id: customButton
                text: qsTr("Custom")
                Layout.rightMargin: 40
                padding: 10
                checkable: true
                checked: Chiaki.window.videoPreset == ChiakiWindow.VideoPreset.Custom
                onToggled: {
                    Chiaki.window.videoPreset = ChiakiWindow.VideoPreset.Custom
                    Chiaki.settings.videoPreset = ChiakiWindow.VideoPreset.Custom
                }
                KeyNavigation.left: highQualityAdvancedSpatialButton
                KeyNavigation.right: displaySettingsButton
                Keys.onReturnPressed: toggled()
                Keys.onEscapePressed: streamMenuWindow.closeRequested()
            }

            ToolButton {
                id: displaySettingsButton
                text: qsTr("Display")
                padding: 10
                checkable: false
                icon.source: "qrc:/icons/settings-20px.svg"
                onClicked: streamMenuWindow.displaySettingsRequested()
                KeyNavigation.left: customButton
                KeyNavigation.right: Chiaki.window.videoPreset == ChiakiWindow.VideoPreset.Custom ? placeboSettingsButton : displaySettingsButton
                Keys.onReturnPressed: clicked()
                Keys.onEscapePressed: streamMenuWindow.closeRequested()
            }

            ToolButton {
                id: placeboSettingsButton
                text: qsTr("Placebo")
                icon.source: "qrc:/icons/settings-20px.svg"
                padding: 10
                checkable: false
                visible: Chiaki.window.videoPreset == ChiakiWindow.VideoPreset.Custom
                onClicked: streamMenuWindow.placeboSettingsRequested()
                KeyNavigation.left: displaySettingsButton
                Keys.onReturnPressed: clicked()
                Keys.onEscapePressed: streamMenuWindow.closeRequested()
            }
        }

        Label {
            anchors {
                right: consoleNameLabel.right
                bottom: consoleNameLabel.top
                bottomMargin: 5
            }
            text: "Mbps"
            font.pixelSize: 18
            visible: Chiaki.session

            Label {
                anchors {
                    right: parent.left
                    baseline: parent.baseline
                    rightMargin: 5
                }
                text: visible && Chiaki.session ? Chiaki.session.measuredBitrate.toFixed(1) : ""
                color: Material.accent
                font.bold: true
                font.pixelSize: 28
            }
        }

        Label {
            id: consoleNameLabel
            anchors {
                right: parent.right
                verticalCenter: parent.verticalCenter
                rightMargin: 30
            }
            text: {
                if (!Chiaki.session)
                    return "";
                if (Chiaki.session.connected)
                    return qsTr("Connected to <b>%1</b>").arg(Chiaki.settings.streamerMode ? "hidden" : Chiaki.session.host);
                return qsTr("Connecting to <b>%1</b>").arg(Chiaki.settings.streamerMode ? "hidden" : Chiaki.session.host);
            }

            RowLayout {
                anchors {
                    right: parent.right
                    top: parent.bottom
                    topMargin: 12
                }

                Label {
                    text: qsTr("packet loss")
                    font.pixelSize: 15
                    opacity: parent.visible && Chiaki.session ? 1.0 : 0.0
                    visible: opacity

                    Behavior on opacity { NumberAnimation { duration: 250 } }

                    Label {
                        anchors {
                            right: parent.left
                            baseline: parent.baseline
                            rightMargin: 5
                        }
                        text: visible ? "%1<font size=\"1\">%</font>".arg((((Chiaki.session && isFinite(Chiaki.session.averagePacketLoss)) ? Chiaki.session.averagePacketLoss : 0) * 100).toFixed(1)) : ""
                        color: "#ef9a9a"
                        font.bold: true
                        font.pixelSize: 18
                    }
                }

                Label {
                    Layout.leftMargin: droppedFramesLabel.width + 6
                    text: qsTr("dropped frames")
                    font.pixelSize: 15
                    opacity: parent.visible && Chiaki.window.droppedFrames ? 1.0 : 0.0
                    visible: opacity

                    Behavior on opacity { NumberAnimation { duration: 250 } }

                    Label {
                        id: droppedFramesLabel
                        anchors {
                            right: parent.left
                            baseline: parent.baseline
                            rightMargin: 5
                        }
                        text: visible ? Chiaki.window.droppedFrames : ""
                        color: "#ef9a9a"
                        font.bold: true
                        font.pixelSize: 18
                    }
                }
            }
        }
    }
}
