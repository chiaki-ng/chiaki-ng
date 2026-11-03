import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Window
import QtQuick.Shapes

import org.streetpea.chiaking

import "controls" as C

Item {
    id: view

    readonly property var hostWindow: view.Window.window
    property bool sessionError: false
    property bool sessionLoading: true
    property list<Item> restoreFocusItems
    readonly property bool useSeparateMenuWindow: Chiaki.window.runtimeRendererBackend === 1
    readonly property int streamMenuHeight: 200
    readonly property bool streamStatsVisible: Chiaki.settings.showStreamStats && Chiaki.session && !(menuController.open || menuController.closing) && !sessionLoading && !sessionError && !(Chiaki.settings.audioVideoDisabled & 0x02)
    property int separateMenuX: 0
    property int separateMenuY: 0
    property int separateMenuWidth: 0
    property int separateStatsX: 0
    property int separateStatsY: 0
    property int separateStatsWidth: 0
    property int separateStatsHeight: 0
    property int separateDialogX: 0
    property int separateDialogY: 0
    property bool sessionStopDialogActive: false
    property bool sessionPinDialogActive: false

    function grabInput(item) {
        Chiaki.window.grabInput();
        restoreFocusItems.push(hostWindow ? hostWindow.activeFocusItem : null);
        if (item)
            item.forceActiveFocus(Qt.TabFocusReason);
    }

    function releaseInput() {
        Chiaki.window.releaseInput();
        let item = restoreFocusItems.pop();
        if (item && item.visible)
            item.forceActiveFocus(Qt.TabFocusReason);
    }

    function updateSeparateMenuGeometry() {
        if (!hostWindow)
            return;
        const topLeft = view.mapToGlobal(0, 0);
        if (useSeparateMenuWindow) {
            separateMenuX = Math.round(topLeft.x);
            separateMenuY = Math.round(topLeft.y + view.height - streamMenuHeight);
            separateMenuWidth = Math.round(view.width);
            separateStatsX = Math.round(topLeft.x);
            separateStatsY = Math.round(topLeft.y);
            separateStatsWidth = Math.round(view.width);
            separateStatsHeight = Math.round(view.height);
        }
    }

    function updateSeparateDialogGeometry(width, height) {
        if (!useSeparateMenuWindow || !hostWindow)
            return;
        separateDialogX = Math.round(hostWindow.x + (hostWindow.width - width) / 2);
        separateDialogY = Math.round(hostWindow.y + (hostWindow.height - height) / 2);
    }

    function updateOverlayInteractionActive() {
        Chiaki.window.setOverlayInteractionActive(
            menuController.open ||
            menuController.closing ||
            sessionStopDialogActive ||
            separateSessionStopWindow.visible ||
            sessionPinDialogActive ||
            separateSessionPinWindow.visible
        );
    }

    StackView.onActivating: {
        Chiaki.window.keepVideo = true;
        sessionError = false;
        errorTitleLabel.text = "";
        errorTextLabel.text = "";
        sessionLoading = !(Chiaki.window.loadingTransitionComplete || (Chiaki.settings.audioVideoDisabled & 0x02));
    }
    StackView.onDeactivated: Chiaki.window.keepVideo = false

    Component.onCompleted: {
        updateSeparateMenuGeometry();
        updateOverlayInteractionActive();
        Chiaki.window.setStatsOverlayActive(streamStatsVisible);
    }
    onStreamStatsVisibleChanged: {
        if (Chiaki.window)
            Chiaki.window.setStatsOverlayActive(streamStatsVisible);
    }
    onWidthChanged: updateSeparateMenuGeometry()
    onHeightChanged: updateSeparateMenuGeometry()
    onUseSeparateMenuWindowChanged: updateSeparateMenuGeometry()

    Connections {
        target: view.hostWindow
        function onXChanged() { view.updateSeparateMenuGeometry() }
        function onYChanged() { view.updateSeparateMenuGeometry() }
        function onWidthChanged() { view.updateSeparateMenuGeometry() }
        function onHeightChanged() { view.updateSeparateMenuGeometry() }
        function onVisibilityChanged() { view.updateSeparateMenuGeometry() }
    }

    QtObject {
        id: menuController
        property bool closing: false
        property bool open: false

        property real lastToggleTime: 0
        function toggle() {
            var now = Date.now();
            if (now - lastToggleTime < 200)
                return;
            lastToggleTime = now;
            if (open)
                close();
            else {
                if (useSeparateMenuWindow) {
                    view.updateSeparateMenuGeometry();
                    view.grabInput(null);
                }
                open = true;
            }
            view.updateOverlayInteractionActive();
        }

        function close() {
            if (!open || closing)
                return;
            closing = true;
            open = false;
            view.releaseInput();
            view.updateOverlayInteractionActive();
        }
    }

    Rectangle {
        id: loadingView
        anchors.fill: parent
        color: "black"
        opacity: sessionError || sessionLoading || (Chiaki.settings.audioVideoDisabled & 0x02) ? 1.0 : 0.0
        visible: opacity

        Behavior on opacity { NumberAnimation { duration: 250 } }

        C.AppBackground {
            anchors.fill: parent
        }

        ColumnLayout {
            anchors.centerIn: parent
            width: Math.min(parent.width - 80, 960)
            spacing: 14

            Item {
                id: spinner
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 88
                Layout.preferredHeight: 88
                Layout.bottomMargin: 18
                visible: sessionLoading

                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    color: "transparent"
                    border.width: 4
                    border.color: C.Theme.stroke
                }

                Shape {
                    anchors.fill: parent
                    preferredRendererType: Shape.GeometryRenderer

                    ShapePath {
                        strokeColor: C.Theme.accent
                        strokeWidth: 4
                        fillColor: "transparent"
                        capStyle: ShapePath.RoundCap
                        PathAngleArc {
                            centerX: 44
                            centerY: 44
                            radiusX: 42
                            radiusY: 42
                            startAngle: -90
                            sweepAngle: 110
                        }
                    }

                    // Only spins while the loading screen is up (no video yet)
                    RotationAnimator on rotation {
                        from: 0
                        to: 360
                        duration: 1000
                        loops: Animation.Infinite
                        running: sessionLoading && loadingView.visible
                    }
                }

                Image {
                    anchors.centerIn: parent
                    source: C.Theme.icon("gamepad")
                    sourceSize: Qt.size(30, 30)
                    opacity: 0.85
                }
            }

            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 72
                Layout.preferredHeight: 72
                Layout.bottomMargin: 10
                radius: 24
                visible: errorTitleLabel.visible || audioVideoDisabledTitleLabel.visible
                color: errorTitleLabel.visible ? C.Theme.dangerSoft : C.Theme.accentSoft
                border.width: 1
                border.color: errorTitleLabel.visible ? "#59FB7185" : "#594D8DFF"

                Image {
                    anchors.centerIn: parent
                    source: C.Theme.icon(errorTitleLabel.visible ? "alert" : "monitor")
                    sourceSize: Qt.size(32, 32)
                }
            }

            Label {
                Layout.alignment: Qt.AlignHCenter
                visible: sessionLoading
                text: {
                    if (!Chiaki.session)
                        return qsTr("Connecting…");
                    return qsTr("Connecting to %1").arg(Chiaki.settings.streamerMode ? "hidden" : Chiaki.session.host);
                }
                font.pixelSize: C.Theme.fontTitle
                font.weight: Font.Bold
                color: C.Theme.text
            }

            Label {
                Layout.alignment: Qt.AlignHCenter
                Layout.maximumWidth: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                lineHeight: 1.25
                font.pixelSize: C.Theme.fontLabel
                color: C.Theme.textSecondary
                text: {
                    if(Chiaki.settings.dpadTouchEnabled)
                    {
                        if(Chiaki.settings.audioVideoDisabled == 0x01)
                            qsTr("Audio Disabled in settings\n") + qsTr("Press %1 to open stream menu").arg(Chiaki.controllers.length ? Chiaki.settings.stringForStreamMenuShortcut() : "Ctrl+O") + "\n" + qsTr("Press %1 to toggle between regular dpad and dpad touch").arg(Chiaki.settings.stringForDpadShortcut())
                        else
                            qsTr("Press %1 to open stream menu").arg(Chiaki.controllers.length ? Chiaki.settings.stringForStreamMenuShortcut() : "Ctrl+O") + "\n" + qsTr("Press %1 to toggle between regular dpad and dpad touch").arg(Chiaki.settings.stringForDpadShortcut())
                    }
                    else
                    {
                        if(Chiaki.settings.audioVideoDisabled == 0x01)
                            qsTr("Audio Disabled in settings\n") + qsTr("Press %1 to open stream menu").arg(Chiaki.controllers.length ? Chiaki.settings.stringForStreamMenuShortcut() : "Ctrl+O")
                        else
                            qsTr("Press %1 to open stream menu").arg(Chiaki.controllers.length ? Chiaki.settings.stringForStreamMenuShortcut() : "Ctrl+O")
                    }
                }
                visible: sessionLoading
            }

            Label {
                id: audioVideoDisabledTitleLabel
                Layout.alignment: Qt.AlignHCenter
                text: (Chiaki.settings.audioVideoDisabled & 0x01) ? qsTr("Audio and Video Disabled") : qsTr("Video Disabled")
                font.pixelSize: C.Theme.fontTitle
                font.weight: Font.Bold
                color: C.Theme.text
                visible: !sessionLoading && !sessionError && (Chiaki.settings.audioVideoDisabled & 0x02)
            }

            Label {
                id: audioVideoDisabledTextLabel
                Layout.alignment: Qt.AlignHCenter
                Layout.maximumWidth: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                lineHeight: 1.25
                font.pixelSize: C.Theme.fontLabel
                color: C.Theme.textSecondary
                text: (Chiaki.settings.audioVideoDisabled & 0x01) ? qsTr("You have disabled audio and video in your settings.\nTo re-enable change Audio/Video to Audio and Video Enabled in the General tab of the settings.") : qsTr("You have disabled video in your settings.\nTo re-enable change Audio/Video to Audio and Video Enabled in the General tab of the settings.")
                visible: !sessionLoading && !sessionError && (Chiaki.settings.audioVideoDisabled & 0x02)
            }

            Label {
                id: errorTitleLabel
                Layout.alignment: Qt.AlignHCenter
                font.pixelSize: C.Theme.fontTitle
                font.weight: Font.Bold
                color: C.Theme.text
                visible: text
                onVisibleChanged: if (visible) view.grabInput(errorTitleLabel)
                Keys.onReturnPressed: root.showMainView()
                Keys.onEscapePressed: root.showMainView()
            }

            Label {
                id: errorTextLabel
                Layout.alignment: Qt.AlignHCenter
                Layout.maximumWidth: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                lineHeight: 1.25
                font.pixelSize: C.Theme.fontLabel
                color: C.Theme.textSecondary
                visible: text
            }
        }
    }

    Rectangle {
        anchors.fill: cantDisplayMessage
        anchors.margins: -36
        radius: C.Theme.radiusXL
        color: C.Theme.surface
        border.width: 1
        border.color: C.Theme.strokeStrong
        opacity: cantDisplayMessage.opacity
        visible: cantDisplayMessage.visible
    }

    ColumnLayout {
        id: cantDisplayMessage
        anchors.centerIn: parent
        opacity: Chiaki.window.hasVideo && Chiaki.session && Chiaki.session.cantDisplay ? 1.0 : 0.0
        visible: opacity
        spacing: 30

        Behavior on opacity { NumberAnimation { duration: 250 } }

        onVisibleChanged: {
            if (visible) {
                menuController.close();
                view.grabInput(goToHomeButton);
            } else {
                view.releaseInput();
            }
        }

        Label {
            Layout.alignment: Qt.AlignCenter
            text: qsTr("The screen contains content that can't be displayed using Remote Play.")
            font.pixelSize: C.Theme.fontBody
            font.weight: Font.DemiBold
            color: C.Theme.text
        }

        C.PillButton {
            id: goToHomeButton
            Layout.alignment: Qt.AlignCenter
            Layout.preferredHeight: 56
            text: qsTr("Go to Home Screen")
            kind: "primary"
            onClicked: Chiaki.sessionGoHome()
            Keys.onReturnPressed: clicked()
            Keys.onEscapePressed: clicked()
        }
    }

    Rectangle {
        anchors {
            right: parent.right
            top: parent.top
            margins: 28
        }
        width: networkRow.implicitWidth + 32
        height: 44
        radius: height / 2
        color: "#EB12151F"
        border.width: 1
        border.color: "#66FBBF24"
        opacity: networkIndicatorTimer.running ? 1.0 : 0.0
        visible: opacity

        Behavior on opacity { NumberAnimation { duration: 400 } }

        Row {
            id: networkRow
            anchors.centerIn: parent
            spacing: 10

            Image {
                anchors.verticalCenter: parent.verticalCenter
                source: C.Theme.icon("radar-off")
                sourceSize: Qt.size(20, 20)
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Unstable connection")
                font.family: C.Theme.fontFamily
                font.pixelSize: C.Theme.fontLabel
                font.weight: Font.DemiBold
                color: C.Theme.warning
            }
        }

        Timer {
            id: networkIndicatorTimer
            running: Chiaki.session?.averagePacketLoss > (Chiaki.settings.wifiDroppedNotif * 0.01)
            interval: 400
        }
    }

    Component {
        id: streamStatsContent
        Item {
            id: streamStatsContentRoot
            anchors.fill: parent
            ColumnLayout {
                anchors {
                    right: parent.right
                    verticalCenter: parent.verticalCenter
                    rightMargin: 5
                }

                Label {
                    Layout.alignment: Qt.AlignRight
                    text: "Mbps"
                    font.pixelSize: 18
                    visible: Chiaki.session ? true : false

                    Label {
                        anchors {
                            right: parent.left
                            baseline: parent.baseline
                            rightMargin: 5
                        }
                        text: parent.visible ? Chiaki.session.measuredBitrate.toFixed(1) : ""
                        color: Material.accent
                        font.bold: true
                        font.pixelSize: 28
                    }
                }

                Label {
                    Layout.alignment: Qt.AlignRight
                    text: qsTr("queue depth avg")
                    font.pixelSize: 15
                    opacity: Chiaki.session ? 1 : 0
                    visible: opacity > 0

                    Behavior on opacity { NumberAnimation { duration: 250 } }

                    Label {
                        anchors {
                            right: parent.left
                            baseline: parent.baseline
                            rightMargin: 5
                        }
                        text: parent.visible ? Chiaki.window.queueDepthAverage.toFixed(1) : ""
                        font.bold: true
                        color: "#90caf9"
                        font.pixelSize: 18
                    }
                }

                Label {
                    Layout.alignment: Qt.AlignRight
                    text: qsTr("pending frame age")
                    font.pixelSize: 15
                    opacity: Chiaki.session ? 1 : 0
                    visible: opacity > 0

                    Behavior on opacity { NumberAnimation { duration: 250 } }

                    Label {
                        anchors {
                            right: parent.left
                            baseline: parent.baseline
                            rightMargin: 5
                        }
                        text: parent.visible ? qsTr("%1 ms").arg((Chiaki.window.pendingFrameAge * 1000.0).toFixed(0)) : ""
                        font.bold: true
                        color: "#90caf9"
                        font.pixelSize: 18
                    }
                }

                Label {
                    Layout.alignment: Qt.AlignRight
                    id: statsPacketLossLabel
                    text: qsTr("packet loss")
                    font.pixelSize: 15
                    opacity: Chiaki.session ? 1 : 0
                    visible: opacity > 0

                    Behavior on opacity { NumberAnimation { duration: 250 } }

                    Label {
                        anchors {
                            right: parent.left
                            baseline: parent.baseline
                            rightMargin: 5
                        }
                        text: parent.visible ? "%1<font size=\"1\">%</font>".arg((((Chiaki.session && isFinite(Chiaki.session.averagePacketLoss)) ? Chiaki.session.averagePacketLoss : 0) * 100).toFixed(1)) : ""
                        font.bold: true
                        color: "#ef9a9a"
                        font.pixelSize: 18
                    }
                }

                Label {
                    Layout.alignment: Qt.AlignRight
                    text: qsTr("dropped frames")
                    font.pixelSize: 15
                    opacity: Chiaki.session ? 1 : 0
                    visible: opacity > 0

                    Behavior on opacity { NumberAnimation { duration: 250 } }

                    Label {
                        id: statsDroppedFramesLabel
                        anchors {
                            right: parent.left
                            baseline: parent.baseline
                            rightMargin: 5
                        }
                        text: parent.visible ? Chiaki.window.droppedFrames : ""
                        color: "#ef9a9a"
                        font.bold: true
                        font.pixelSize: 18
                    }
                }

                Label {
                    Layout.alignment: Qt.AlignRight
                    text: qsTr("lost frames")
                    font.pixelSize: 15
                    opacity: Chiaki.session ? 1 : 0
                    visible: opacity > 0

                    Behavior on opacity { NumberAnimation { duration: 250 } }

                    Label {
                        anchors {
                            right: parent.left
                            baseline: parent.baseline
                            rightMargin: 5
                        }
                        text: parent.visible ? ((Chiaki.session && isFinite(Chiaki.session.framesLost)) ? Chiaki.session.framesLost : 0) : ""
                        color: "#ef9a9a"
                        font.bold: true
                        font.pixelSize: 18
                    }
                }
            }
        }
    }

    Component {
        id: menuContentComponent

        StreamMenuBar {
            anchors.fill: parent
            active: menuView.visible
            onCloseRequested: menuController.close()
            onMainViewRequested: root.showMainView()
            onDisplaySettingsRequested: {
                menuController.close();
                root.openDisplaySettings();
            }
            onPlaceboSettingsRequested: {
                menuController.close();
                root.openPlaceboSettings();
            }
        }
    }

    Item {
        id: menuView
        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        height: streamMenuHeight
        y: menuController.open ? parent.height - height : parent.height
        visible: !useSeparateMenuWindow && (menuController.open || menuController.closing)
        enabled: !useSeparateMenuWindow && menuController.open
        onVisibleChanged: {
            if (visible)
                view.grabInput(inlineMenuContent.item ? inlineMenuContent.item.initialFocusItem : null);
        }

        Behavior on y {
            NumberAnimation {
                id: inlineMenuAnimation
                duration: 250
                onRunningChanged: {
                    if (!running && !menuController.open) {
                        menuController.closing = false;
                        view.updateOverlayInteractionActive();
                    }
                }
            }
        }

        Loader {
            id: inlineMenuContent
            anchors.fill: parent
            active: !useSeparateMenuWindow
            sourceComponent: menuContentComponent
        }
    }

    StreamMenuWindow {
        id: separateMenuWindow
        transientParent: view.hostWindow
        x: separateMenuX
        y: menuController.open ? separateMenuY : separateMenuY + streamMenuHeight
        width: separateMenuWidth > 0 ? separateMenuWidth : view.width
        height: streamMenuHeight
        open: useSeparateMenuWindow && menuController.open
        closing: useSeparateMenuWindow && menuController.closing
        onCloseRequested: menuController.close()
        onDisplaySettingsRequested: {
            menuController.close();
            root.openDisplaySettings();
        }
        onPlaceboSettingsRequested: {
            menuController.close();
            root.openPlaceboSettings();
        }
        onMainViewRequested: root.showMainView()
        onCloseAnimationFinished: {
            if (view.hostWindow)
                view.hostWindow.requestActivate();
            menuController.closing = false;
            view.updateOverlayInteractionActive();
        }
    }

    Rectangle {
        anchors.fill: parent
        color: C.Theme.scrim
        opacity: sessionStopDialog.visible || sessionPinDialog.visible ? 1.0 : 0.0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: C.Theme.durMed } }
    }

    Popup {
        id: sessionStopDialog
        property int closeAction: 0
        parent: Overlay.overlay
        x: Math.round((root.width - width) / 2)
        y: Math.round((root.height - height) / 2)
        modal: true
        padding: 40
        topPadding: 36
        onAboutToShow: {
            closeAction = 0;
            sessionStopDialogActive = true;
            view.updateOverlayInteractionActive();
        }
        onClosed: {
            view.releaseInput();
            sessionStopDialogActive = false;
            view.updateOverlayInteractionActive();
            if (closeAction)
                Chiaki.stopSession(closeAction == 1);
        }

        dim: false

        enter: Transition {
            NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: C.Theme.durMed; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: 0.94; to: 1.0; duration: C.Theme.durSlow; easing.type: Easing.OutBack }
        }

        exit: Transition {
            NumberAnimation { property: "opacity"; from: 1.0; to: 0.0; duration: C.Theme.durFast }
        }

        background: Rectangle {
            radius: C.Theme.radiusXL
            color: C.Theme.surface
            border.width: 1
            border.color: C.Theme.strokeStrong
        }

        ColumnLayout {
            spacing: 0

            Rectangle {
                Layout.alignment: Qt.AlignCenter
                Layout.preferredWidth: 60
                Layout.preferredHeight: 60
                Layout.bottomMargin: 18
                radius: 20
                color: C.Theme.accentSoft
                border.width: 1
                border.color: "#594D8DFF"

                Image {
                    anchors.centerIn: parent
                    source: C.Theme.icon("moon")
                    sourceSize: Qt.size(28, 28)
                }
            }

            Label {
                Layout.alignment: Qt.AlignCenter
                text: qsTr("Disconnect Session")
                font.pixelSize: C.Theme.fontHeadline
                font.weight: Font.Bold
                color: C.Theme.text
            }

            Label {
                Layout.topMargin: 8
                Layout.alignment: Qt.AlignCenter
                text: qsTr("Do you want the Console to go into sleep mode?")
                font.pixelSize: C.Theme.fontBody
                color: C.Theme.textSecondary
            }

            RowLayout {
                Layout.topMargin: 30
                Layout.alignment: Qt.AlignCenter
                spacing: 16

                C.PillButton {
                    id: sleepButton
                    Layout.preferredWidth: 200
                    Layout.preferredHeight: 58
                    text: qsTr("Sleep")
                    iconSource: C.Theme.icon("moon")
                    emphasized: activeFocus
                    KeyNavigation.right: noButton
                    Keys.onReturnPressed: clicked()
                    Keys.onEscapePressed: sessionStopDialog.close()
                    onVisibleChanged: if (visible) view.grabInput(sleepButton)
                    onClicked: {
                        sessionStopDialog.closeAction = 1;
                        sessionStopDialog.close();
                    }
                }

                C.PillButton {
                    id: noButton
                    Layout.preferredWidth: 200
                    Layout.preferredHeight: 58
                    text: qsTr("No")
                    iconSource: C.Theme.icon("power")
                    emphasized: activeFocus
                    KeyNavigation.left: sleepButton
                    Keys.onReturnPressed: clicked()
                    Keys.onEscapePressed: sessionStopDialog.close()
                    onClicked: {
                        sessionStopDialog.closeAction = 2;
                        sessionStopDialog.close();
                    }
                }
            }
        }
    }

    Window {
        id: separateSessionStopWindow
        property int closeAction: 0
        readonly property int dialogWidth: 640
        readonly property int dialogHeight: 320
        visible: false
        flags: Qt.Dialog | Qt.FramelessWindowHint
        color: "transparent"
        transientParent: view.hostWindow
        modality: Qt.ApplicationModal
        x: separateDialogX
        y: separateDialogY
        width: dialogWidth
        height: dialogHeight

        onVisibleChanged: {
            if (visible) {
                closeAction = 0;
                view.updateSeparateDialogGeometry(width, height);
                requestActivate();
                view.grabInput(separateSleepButton);
            } else {
                view.releaseInput();
                if (closeAction)
                    Chiaki.stopSession(closeAction === 1);
            }
            view.updateOverlayInteractionActive();
        }

        Rectangle {
            anchors.fill: parent
            radius: C.Theme.radiusXL
            color: C.Theme.surface
            border.color: C.Theme.strokeStrong
            border.width: 1
        }

        ColumnLayout {
            anchors.centerIn: parent
            spacing: 0

            Rectangle {
                Layout.alignment: Qt.AlignCenter
                Layout.preferredWidth: 60
                Layout.preferredHeight: 60
                Layout.bottomMargin: 18
                radius: 20
                color: C.Theme.accentSoft
                border.width: 1
                border.color: "#594D8DFF"

                Image {
                    anchors.centerIn: parent
                    source: C.Theme.icon("moon")
                    sourceSize: Qt.size(28, 28)
                }
            }

            Label {
                Layout.alignment: Qt.AlignCenter
                text: qsTr("Disconnect Session")
                font.pixelSize: C.Theme.fontHeadline
                font.weight: Font.Bold
                color: C.Theme.text
            }

            Label {
                Layout.topMargin: 8
                Layout.alignment: Qt.AlignCenter
                text: qsTr("Do you want the Console to go into sleep mode?")
                font.pixelSize: C.Theme.fontBody
                color: C.Theme.textSecondary
            }

            RowLayout {
                Layout.topMargin: 30
                Layout.alignment: Qt.AlignCenter
                spacing: 16

                C.PillButton {
                    id: separateSleepButton
                    Layout.preferredWidth: 200
                    Layout.preferredHeight: 58
                    text: qsTr("Sleep")
                    iconSource: C.Theme.icon("moon")
                    emphasized: activeFocus
                    KeyNavigation.right: separateNoButton
                    Keys.onReturnPressed: clicked()
                    Keys.onEscapePressed: separateSessionStopWindow.visible = false
                    onClicked: {
                        separateSessionStopWindow.closeAction = 1;
                        separateSessionStopWindow.visible = false;
                    }
                }

                C.PillButton {
                    id: separateNoButton
                    Layout.preferredWidth: 200
                    Layout.preferredHeight: 58
                    text: qsTr("No")
                    iconSource: C.Theme.icon("power")
                    emphasized: activeFocus
                    KeyNavigation.left: separateSleepButton
                    Keys.onReturnPressed: clicked()
                    Keys.onEscapePressed: separateSessionStopWindow.visible = false
                    onClicked: {
                        separateSessionStopWindow.closeAction = 2;
                        separateSessionStopWindow.visible = false;
                    }
                }
            }
        }
    }

    C.ModalDialog {
        id: sessionPinDialog
        title: qsTr("Console Login PIN")
        iconSource: C.Theme.icon("pin")
        closePolicy: Popup.NoAutoClose
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAboutToShow: {
            standardButton(Dialog.Ok).enabled = Qt.binding(function() {
                return pinField.acceptableInput;
            });
            view.grabInput(pinField);
            sessionPinDialogActive = true;
            view.updateOverlayInteractionActive();
        }
        onClosed: {
            view.releaseInput();
            sessionPinDialogActive = false;
            view.updateOverlayInteractionActive();
        }
        onAccepted: Chiaki.enterPin(pinField.text)
        onRejected: Chiaki.stopSession(false)

        TextField {
            id: pinField
            echoMode: Chiaki.settings.streamerMode ? TextInput.Password : TextInput.Normal
            implicitWidth: 320
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: C.Theme.fontTitle
            font.weight: Font.Bold
            font.letterSpacing: 12
            validator: RegularExpressionValidator { regularExpression: /[0-9]{4}/ }
            Keys.onReturnPressed: {
                if(sessionPinDialog.standardButton(Dialog.Ok).enabled)
                    sessionPinDialog.standardButton(Dialog.Ok).clicked()
            }
        }
    }

    Window {
        id: separateSessionPinWindow
        readonly property int dialogWidth: 420
        readonly property int dialogHeight: 280
        visible: false
        flags: Qt.Dialog | Qt.FramelessWindowHint
        color: "transparent"
        transientParent: view.hostWindow
        modality: Qt.ApplicationModal
        x: separateDialogX
        y: separateDialogY
        width: dialogWidth
        height: dialogHeight

        onVisibleChanged: {
            if (visible) {
                separatePinField.text = "";
                view.updateSeparateDialogGeometry(width, height);
                requestActivate();
                view.grabInput(separatePinField);
            } else {
                view.releaseInput();
            }
            view.updateOverlayInteractionActive();
        }

        Rectangle {
            anchors.fill: parent
            radius: C.Theme.radiusXL
            color: C.Theme.surface
            border.color: C.Theme.strokeStrong
            border.width: 1
        }

        ColumnLayout {
            anchors {
                fill: parent
                margins: 28
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 14

                Rectangle {
                    Layout.preferredWidth: 44
                    Layout.preferredHeight: 44
                    radius: 15
                    color: C.Theme.accentSoft
                    border.width: 1
                    border.color: "#594D8DFF"

                    Image {
                        anchors.centerIn: parent
                        source: C.Theme.icon("pin")
                        sourceSize: Qt.size(22, 22)
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Console Login PIN")
                    font.pixelSize: C.Theme.fontHeadline
                    font.weight: Font.Bold
                    color: C.Theme.text
                }
            }

            TextField {
                id: separatePinField
                Layout.topMargin: 16
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: C.Theme.fontTitle
                font.weight: Font.Bold
                font.letterSpacing: 12
                echoMode: Chiaki.settings.streamerMode ? TextInput.Password : TextInput.Normal
                validator: RegularExpressionValidator { regularExpression: /[0-9]{4}/ }
                Keys.onReturnPressed: if (acceptableInput) separatePinOkButton.clicked()
                Keys.onEscapePressed: separateSessionPinWindow.visible = false
            }

            RowLayout {
                Layout.topMargin: 16
                Layout.alignment: Qt.AlignRight
                spacing: 12

                C.PillButton {
                    text: qsTr("Cancel")
                    emphasized: activeFocus
                    Keys.onReturnPressed: clicked()
                    onClicked: {
                        separateSessionPinWindow.visible = false;
                        Chiaki.stopSession(false);
                    }
                }

                C.PillButton {
                    id: separatePinOkButton
                    text: qsTr("OK")
                    kind: "primary"
                    enabled: separatePinField.acceptableInput
                    Keys.onReturnPressed: clicked()
                    onClicked: {
                        const pin = separatePinField.text;
                        separateSessionPinWindow.visible = false;
                        Chiaki.enterPin(pin);
                    }
                }
            }
        }
    }

    Timer {
        id: closeTimer
        interval: 2000
        onTriggered: root.showMainView()
    }

    Connections {
        target: Chiaki

        function onSessionChanged() {
            if (!Chiaki.session) {
                if (errorTitleLabel.text)
                    closeTimer.start();
                else
                    root.showMainView();
            } else {
                sessionError = false;
                errorTitleLabel.text = "";
                errorTextLabel.text = "";
                sessionLoading = !(Chiaki.window.loadingTransitionComplete || (Chiaki.settings.audioVideoDisabled & 0x02));
            }
        }

        function onSessionError(title, text) {
            sessionError = true;
            sessionLoading = false;
            errorTitleLabel.text = title;
            errorTextLabel.text = text;
            closeTimer.start();
        }

        function onSessionPinDialogRequested() {
            if (sessionPinDialog.opened || separateSessionPinWindow.visible)
                return;
            menuController.close();
            if (useSeparateMenuWindow)
                separateSessionPinWindow.visible = true;
            else
                sessionPinDialog.open();
            Chiaki.window.requestOverlayUpdate();
        }

        function onSessionStopDialogRequested() {
            if (sessionStopDialog.opened || separateSessionStopWindow.visible)
                return;
            menuController.close();
            if (useSeparateMenuWindow)
                separateSessionStopWindow.visible = true;
            else
                sessionStopDialog.open();
            Chiaki.window.requestOverlayUpdate();
        }
    }

    Connections {
        target: Chiaki.window

        function onLoadingTransitionCompleteChanged() {
            if (Chiaki.window.loadingTransitionComplete) {
                sessionLoading = false;
                Chiaki.window.noteLoadingTransitionComplete();
                Chiaki.window.requestOverlayUpdate();
            } else if (Chiaki.session) {
                sessionLoading = !(Chiaki.settings.audioVideoDisabled & 0x02);
            }
        }

        function onMenuRequested() {
            if (sessionPinDialog.opened || sessionStopDialog.opened || separateSessionPinWindow.visible || separateSessionStopWindow.visible)
                return;
            menuController.toggle();
            Chiaki.window.requestOverlayUpdate();
        }
    }
    Connections {
        target: Chiaki

        function onSessionChanged() {
            if (!Chiaki.session)
                menuController.close();
        }
    }
    Connections {
        target: Chiaki.session

        function onConnectedChanged() {
            if (Chiaki.settings.audioVideoDisabled & 0x02)
                sessionLoading = false;
        }
    }
}
