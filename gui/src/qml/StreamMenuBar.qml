import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

import org.streetpea.chiaking

import "controls" as C

// In-stream control dock. Shared by the inline overlay menu and the separate menu window.
// Everything here is static or event-driven; nothing animates while the dock is hidden.
Item {
    id: menu

    // Separate windows may not support translucency, so they get a solid backdrop.
    property bool opaque: false
    // Live stats only update while the dock is shown: any change to an item, even a hidden one,
    // makes the overlay re-sync during the stream.
    property bool active: true
    property Item initialFocusItem: closeButton

    signal closeRequested()
    signal mainViewRequested()
    signal displaySettingsRequested()
    signal placeboSettingsRequested()

    readonly property bool zoomMode: Chiaki.window.videoMode == ChiakiWindow.VideoMode.Zoom

    function setPreset(preset) {
        Chiaki.window.videoPreset = preset;
        Chiaki.settings.videoPreset = preset;
    }

    // Backdrop
    Rectangle {
        anchors.fill: parent
        visible: !menu.opaque
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#00050609" }
            GradientStop { position: 0.45; color: "#99050609" }
            GradientStop { position: 1.0; color: "#E6050609" }
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: menu.opaque
        gradient: Gradient {
            GradientStop { position: 0.0; color: C.Theme.bg }
            GradientStop { position: 1.0; color: C.Theme.bgDeep }
        }
    }

    // Session info strip
    Row {
        anchors {
            right: parent.right
            bottom: dock.top
            rightMargin: 28
            bottomMargin: 12
        }
        spacing: 8
        visible: menu.active && !!Chiaki.session

        C.Chip {
            dot: true
            tinted: true
            tone: menu.active && Chiaki.session && Chiaki.session.connected ? C.Theme.success : C.Theme.warning
            text: {
                if (!menu.active || !Chiaki.session)
                    return "";
                const host = Chiaki.settings.streamerMode ? "hidden" : Chiaki.session.host;
                return Chiaki.session.connected ? qsTr("Connected to %1").arg(host) : qsTr("Connecting to %1").arg(host);
            }
        }

        C.Chip {
            tinted: true
            tone: C.Theme.accentBright
            text: menu.active && Chiaki.session ? qsTr("%1 Mbps").arg(Chiaki.session.measuredBitrate.toFixed(1)) : ""
        }

        C.Chip {
            readonly property real loss: (menu.active && Chiaki.session && isFinite(Chiaki.session.averagePacketLoss)) ? Chiaki.session.averagePacketLoss : 0
            tinted: loss > 0
            tone: C.Theme.danger
            text: qsTr("%1% packet loss").arg((loss * 100).toFixed(1))
        }

        C.Chip {
            visible: menu.active && Chiaki.window.droppedFrames > 0
            tinted: true
            tone: C.Theme.danger
            text: visible ? qsTr("%1 dropped frames").arg(Chiaki.window.droppedFrames) : ""
        }
    }

    // Dock
    Rectangle {
        id: dock
        anchors {
            left: parent.left
            bottom: parent.bottom
            leftMargin: 24
            bottomMargin: 20
        }
        width: dockRow.implicitWidth + 24
        height: 112
        // Shrink rather than cut tiles off when they don't all fit (e.g. Zoom + Custom on a 1280px Steam Deck)
        scale: parent.width > 48 ? Math.min(1, (parent.width - 48) / width) : 1
        transformOrigin: Item.BottomLeft
        radius: 28
        color: menu.opaque ? C.Theme.surface : "#EB12151F"
        border.width: 1
        border.color: C.Theme.strokeStrong

        Rectangle {
            anchors {
                top: parent.top
                left: parent.left
                right: parent.right
                margins: 1
            }
            height: parent.height / 2
            radius: parent.radius
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#0FFFFFFF" }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }

        RowLayout {
            id: dockRow
            anchors {
                left: parent.left
                verticalCenter: parent.verticalCenter
                leftMargin: 12
            }
            spacing: 4

            C.DockTile {
                id: closeButton
                text: Chiaki.session ? qsTr("Quit") : qsTr("Back")
                iconSource: C.Theme.icon("power")
                activeFocusOnTab: true
                onClicked: {
                    if (Chiaki.session)
                        Chiaki.window.close();
                    else
                        menu.mainViewRequested();
                }
                KeyNavigation.right: volumeSlider
                Keys.onReturnPressed: clicked()
                Keys.onEscapePressed: menu.closeRequested()
            }

            DockDivider {}

            Item {
                Layout.preferredWidth: 68
                Layout.preferredHeight: 84

                Slider {
                    id: volumeSlider
                    anchors {
                        horizontalCenter: parent.horizontalCenter
                        top: parent.top
                        bottom: volumeCaption.top
                        topMargin: 4
                        bottomMargin: 2
                    }
                    orientation: Qt.Vertical
                    from: 0
                    to: 128
                    padding: 4
                    stepSize: 1
                    value: Chiaki.settings.audioVolume
                    onMoved: Chiaki.settings.audioVolume = value
                    KeyNavigation.left: closeButton
                    KeyNavigation.right: muteButton
                    Keys.onEscapePressed: menu.closeRequested()

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -2
                        radius: 12
                        color: "transparent"
                        border.width: 2
                        border.color: C.Theme.accent
                        visible: volumeSlider.activeFocus
                    }
                }

                // Sits on the same line as the DockTile captions (84px tile, centered 50px content)
                Text {
                    id: volumeCaption
                    anchors {
                        horizontalCenter: parent.horizontalCenter
                        bottom: parent.bottom
                        bottomMargin: 17
                    }
                    text: ((volumeSlider.value / 128.0) * 100).toFixed(0) + "%"
                    font.family: C.Theme.fontFamily
                    font.pixelSize: C.Theme.fontMicro + 1
                    font.weight: Font.DemiBold
                    color: C.Theme.textSecondary
                }
            }

            C.DockTile {
                id: muteButton
                text: qsTr("Mic")
                iconSource: C.Theme.icon(selected ? "mic" : "mic-off")
                enabled: !!(Chiaki.session && Chiaki.session.connected)
                selected: !!(Chiaki.session && !Chiaki.session.muted)
                onClicked: Chiaki.session.muted = !Chiaki.session.muted
                KeyNavigation.left: volumeSlider
                KeyNavigation.right: zoomButton
                Keys.onReturnPressed: clicked()
                Keys.onEscapePressed: menu.closeRequested()
            }

            DockDivider {}

            C.DockTile {
                id: zoomButton
                text: qsTr("Zoom")
                iconSource: C.Theme.icon("zoom")
                selected: menu.zoomMode
                onClicked: Chiaki.window.videoMode = Chiaki.window.videoMode == ChiakiWindow.VideoMode.Zoom ? ChiakiWindow.VideoMode.Normal : ChiakiWindow.VideoMode.Zoom
                KeyNavigation.left: muteButton
                KeyNavigation.right: menu.zoomMode ? zoomFactor : stretchButton
                Keys.onReturnPressed: clicked()
                Keys.onEscapePressed: menu.closeRequested()
            }

            Item {
                Layout.preferredWidth: 96
                Layout.preferredHeight: 84
                visible: menu.zoomMode

                Slider {
                    id: zoomFactor
                    anchors {
                        horizontalCenter: parent.horizontalCenter
                        top: parent.top
                        bottom: zoomCaption.top
                        topMargin: 4
                        bottomMargin: 2
                    }
                    orientation: Qt.Vertical
                    from: -1
                    to: 4
                    padding: 4
                    stepSize: 0.01
                    value: Chiaki.window.ZoomFactor
                    onMoved: {
                        Chiaki.window.ZoomFactor = value
                        Chiaki.settings.sZoomFactor = value
                    }
                    KeyNavigation.left: zoomButton
                    KeyNavigation.right: stretchButton
                    Keys.onEscapePressed: menu.closeRequested()

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -2
                        radius: 12
                        color: "transparent"
                        border.width: 2
                        border.color: C.Theme.accent
                        visible: zoomFactor.activeFocus
                    }
                }

                Text {
                    id: zoomCaption
                    anchors {
                        horizontalCenter: parent.horizontalCenter
                        bottom: parent.bottom
                        bottomMargin: 17
                    }
                    text: {
                        if (zoomFactor.value === -1)
                            return qsTr("No Black Bars");
                        if (zoomFactor.value >= 0)
                            return (zoomFactor.value + 1).toFixed(2) + qsTr(" x");
                        return zoomFactor.value.toFixed(2) + qsTr(" x");
                    }
                    font.family: C.Theme.fontFamily
                    font.pixelSize: C.Theme.fontMicro + 1
                    font.weight: Font.DemiBold
                    color: C.Theme.textSecondary
                }
            }

            C.DockTile {
                id: stretchButton
                text: qsTr("Stretch")
                iconSource: C.Theme.icon("stretch")
                selected: Chiaki.window.videoMode == ChiakiWindow.VideoMode.Stretch
                onClicked: Chiaki.window.videoMode = Chiaki.window.videoMode == ChiakiWindow.VideoMode.Stretch ? ChiakiWindow.VideoMode.Normal : ChiakiWindow.VideoMode.Stretch
                KeyNavigation.left: menu.zoomMode ? zoomFactor : zoomButton
                KeyNavigation.right: defaultButton
                Keys.onReturnPressed: clicked()
                Keys.onEscapePressed: menu.closeRequested()
            }

            DockDivider {}

            C.DockTile {
                id: defaultButton
                text: qsTr("Default")
                level: 1
                selected: Chiaki.window.videoPreset == ChiakiWindow.VideoPreset.Default
                onClicked: menu.setPreset(ChiakiWindow.VideoPreset.Default)
                KeyNavigation.left: stretchButton
                KeyNavigation.right: highQualityButton
                Keys.onReturnPressed: clicked()
                Keys.onEscapePressed: menu.closeRequested()
            }

            C.DockTile {
                id: highQualityButton
                text: qsTr("High Quality")
                level: 2
                selected: Chiaki.window.videoPreset == ChiakiWindow.VideoPreset.HighQuality
                onClicked: menu.setPreset(ChiakiWindow.VideoPreset.HighQuality)
                KeyNavigation.left: defaultButton
                KeyNavigation.right: highQualitySpatialButton
                Keys.onReturnPressed: clicked()
                Keys.onEscapePressed: menu.closeRequested()
            }

            C.DockTile {
                id: highQualitySpatialButton
                text: qsTr("HQ + Spatial")
                level: 3
                selected: Chiaki.window.videoPreset == ChiakiWindow.VideoPreset.HighQualitySpatial
                onClicked: menu.setPreset(ChiakiWindow.VideoPreset.HighQualitySpatial)
                KeyNavigation.left: highQualityButton
                KeyNavigation.right: highQualityAdvancedSpatialButton
                Keys.onReturnPressed: clicked()
                Keys.onEscapePressed: menu.closeRequested()
            }

            C.DockTile {
                id: highQualityAdvancedSpatialButton
                text: qsTr("HQ + Adv Spatial")
                level: 4
                selected: Chiaki.window.videoPreset == ChiakiWindow.VideoPreset.HighQualityAdvancedSpatial
                onClicked: menu.setPreset(ChiakiWindow.VideoPreset.HighQualityAdvancedSpatial)
                KeyNavigation.left: highQualitySpatialButton
                KeyNavigation.right: customButton
                Keys.onReturnPressed: clicked()
                Keys.onEscapePressed: menu.closeRequested()
            }

            C.DockTile {
                id: customButton
                text: qsTr("Custom")
                iconSource: C.Theme.icon("sliders")
                selected: Chiaki.window.videoPreset == ChiakiWindow.VideoPreset.Custom
                onClicked: menu.setPreset(ChiakiWindow.VideoPreset.Custom)
                KeyNavigation.left: highQualityAdvancedSpatialButton
                KeyNavigation.right: displaySettingsButton
                Keys.onReturnPressed: clicked()
                Keys.onEscapePressed: menu.closeRequested()
            }

            DockDivider {}

            C.DockTile {
                id: displaySettingsButton
                text: qsTr("Display")
                iconSource: C.Theme.icon("monitor")
                onClicked: menu.displaySettingsRequested()
                KeyNavigation.left: customButton
                KeyNavigation.right: Chiaki.window.videoPreset == ChiakiWindow.VideoPreset.Custom ? placeboSettingsButton : displaySettingsButton
                Keys.onReturnPressed: clicked()
                Keys.onEscapePressed: menu.closeRequested()
            }

            C.DockTile {
                id: placeboSettingsButton
                text: qsTr("Placebo")
                iconSource: C.Theme.icon("sparkles")
                visible: Chiaki.window.videoPreset == ChiakiWindow.VideoPreset.Custom
                onClicked: menu.placeboSettingsRequested()
                KeyNavigation.left: displaySettingsButton
                Keys.onReturnPressed: clicked()
                Keys.onEscapePressed: menu.closeRequested()
            }
        }
    }

    component DockDivider: Rectangle {
        Layout.preferredWidth: 1
        Layout.preferredHeight: 56
        Layout.leftMargin: 6
        Layout.rightMargin: 6
        color: C.Theme.strokeStrong
    }
}
