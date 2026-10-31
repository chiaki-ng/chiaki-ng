import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Shapes

import org.streetpea.chiaking

import "controls" as C

Pane {
    padding: 0
    id: consolePane
    background: null
    readonly property int visibleHostCount: {
        let n = 0;
        for (let i = 0; i < Chiaki.hosts.length; ++i) {
            if (Chiaki.hosts[i].display)
                ++n;
        }
        return n;
    }
    readonly property bool hasController: Chiaki.controllers.length > 0
    readonly property bool compact: width < 1000
    StackView.onActivated: {
        forceActiveFocus(Qt.TabFocusReason);
        if(!Chiaki.autoConnect && !root.initialAsk && !Chiaki.window.directStream)
        {
            root.initialAsk = true;
            if(Chiaki.settings.addSteamShortcutAsk && (typeof Chiaki.createSteamShortcut === "function"))
                root.showRemindDialog(qsTr("Official Steam artwork + controller layout"), qsTr("Would you like to either create a new non-Steam game for chiaki-ng\nor update an existing non-Steam game with the official artwork and controller layout?") + "\n\n" + qsTr("(Note: If you select no now and want to do this later, click the button or press R3 from the main menu.)"), false, () => root.showSteamShortcutDialog(true));
            else if(Chiaki.settings.remotePlayAsk)
            {
                if(!Chiaki.settings.psnRefreshToken || !Chiaki.settings.psnAuthToken || !Chiaki.settings.psnAuthTokenExpiry || !Chiaki.settings.psnAccountId)
                    root.showRemindDialog(qsTr("Remote Play via PSN"), qsTr("Would you like to connect to PSN?\nThis enables:\n- Automatic registration\n- Playing outside of your home network without port forwarding?") + "\n\n" + qsTr("(Note: If you select no now and want to do this later, go to the Config section of the settings.)"), true, () => root.showPSNTokenDialog(false));
                else
                    Chiaki.settings.remotePlayAsk = false;
            }
        }
    }
    Keys.onUpPressed: {
        if(hostsView.currentItem && hostsView.currentItem.visible)
        {
            hostsView.decrementCurrentIndex()
            while(!hostsView.currentItem.visible)
                hostsView.decrementCurrentIndex()
        }
    }
    Keys.onDownPressed: {
        if(hostsView.currentItem && hostsView.currentItem.visible)
        {
            hostsView.incrementCurrentIndex()
            while(!hostsView.currentItem.visible)
                 hostsView.incrementCurrentIndex()
        }
    }
    Keys.onMenuPressed: settingsButton.clicked()
    Keys.onReturnPressed: if (hostsView.currentItem) hostsView.currentItem.connectToHost()
    Keys.onYesPressed: if (hostsView.currentItem) hostsView.currentItem.wakeUpHost()
    Keys.onNoPressed: if (hostsView.currentItem) hostsView.currentItem.deleteHost()
    Keys.onEscapePressed: root.showConfirmDialog(qsTr("Quit"), qsTr("Are you sure you want to quit?"), () => Qt.quit())
    Keys.onPressed: (event) => {
        if (event.modifiers)
            return;
        switch (event.key) {
        case Qt.Key_PageUp:
            if (hostsView.currentItem) hostsView.currentItem.setConsolePin();
            event.accepted = true;
            break;
        case Qt.Key_PageDown:
            if (Chiaki.settings.psnAuthToken) Chiaki.refreshPsnToken();
            event.accepted = true;
            break;
        case Qt.Key_F1:
            if (typeof Chiaki.createSteamShortcut === "function") root.showSteamShortcutDialog(false);
            event.accepted = true;
            break;
        case Qt.Key_F2:
            root.showManualHostDialog();
            event.accepted = true;
            break;
        }
    }

    // ── Top bar ────────────────────────────────────────────────────────────
    Item {
        id: toolBar
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
        }
        height: 104

        RowLayout {
            anchors {
                fill: parent
                leftMargin: 40
                rightMargin: 32
            }
            spacing: 12

            Rectangle {
                Layout.preferredWidth: 48
                Layout.preferredHeight: 48
                radius: 15
                gradient: Gradient {
                    GradientStop { position: 0.0; color: C.Theme.accent }
                    GradientStop { position: 1.0; color: C.Theme.accentAlt }
                }

                Image {
                    anchors.centerIn: parent
                    source: C.Theme.icon("logo-mark")
                    sourceSize: Qt.size(28, 28)
                }
            }

            ColumnLayout {
                Layout.leftMargin: 6
                spacing: 0

                Text {
                    text: "chiaki-ng"
                    font.family: C.Theme.fontFamily
                    font.pixelSize: C.Theme.fontHeadline
                    font.weight: Font.Bold
                    font.letterSpacing: -0.4
                    color: C.Theme.text
                }

                Text {
                    visible: !consolePane.compact
                    text: qsTr("PlayStation Remote Play")
                    font.family: C.Theme.fontFamily
                    font.pixelSize: C.Theme.fontCaption
                    font.weight: Font.Medium
                    color: C.Theme.textTertiary
                }
            }

            Item { Layout.fillWidth: true }

            C.PillButton {
                text: consolePane.compact ? "" : qsTr("Steam Shortcut")
                kind: consolePane.compact ? "secondary" : "ghost"
                compact: true
                iconSource: consolePane.hasController && !consolePane.compact ? "" : C.Theme.icon("gamepad")
                hint: consolePane.hasController && !consolePane.compact ? "qrc:/icons/l3.svg" : ""
                focusPolicy: Qt.NoFocus
                visible: typeof Chiaki.createSteamShortcut === "function"
                onClicked: root.showSteamShortcutDialog(false)
            }

            C.PillButton {
                text: consolePane.compact ? "" : qsTr("Refresh PSN")
                kind: consolePane.compact ? "secondary" : "ghost"
                compact: true
                iconSource: consolePane.hasController && !consolePane.compact ? "" : C.Theme.icon("refresh")
                hint: consolePane.hasController && !consolePane.compact ? "qrc:/icons/r1.svg" : ""
                focusPolicy: Qt.NoFocus
                visible: Chiaki.settings.psnAuthToken
                onClicked: Chiaki.refreshPsnToken();
            }

            C.PillButton {
                text: consolePane.compact ? "" : qsTr("Add Console")
                compact: true
                iconSource: consolePane.hasController && !consolePane.compact ? "" : C.Theme.icon("plus")
                hint: consolePane.hasController && !consolePane.compact ? "qrc:/icons/r3.svg" : ""
                focusPolicy: Qt.NoFocus
                onClicked: root.showManualHostDialog()
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.preferredHeight: 28
                Layout.leftMargin: 6
                Layout.rightMargin: 6
                color: C.Theme.strokeStrong
            }

            C.IconButton {
                id: settingsButton
                iconSource: C.Theme.icon("settings")
                focusPolicy: Qt.NoFocus
                ToolTip.text: qsTr("Settings")
                onClicked: root.showSettingsDialog()
            }

            C.IconButton {
                iconSource: C.Theme.icon("power")
                focusPolicy: Qt.NoFocus
                ToolTip.text: qsTr("Quit")
                onClicked: Qt.quit()
            }
        }
    }

    // ── Section header ─────────────────────────────────────────────────────
    RowLayout {
        id: sectionHeader
        anchors {
            top: toolBar.bottom
            left: parent.left
            right: parent.right
            leftMargin: 40
            rightMargin: 40
        }
        height: 52
        spacing: 14

        Text {
            text: qsTr("Consoles")
            font.family: C.Theme.fontFamily
            font.pixelSize: C.Theme.fontTitle
            font.weight: Font.Bold
            font.letterSpacing: -0.5
            color: C.Theme.text
        }

        C.Chip {
            Layout.alignment: Qt.AlignVCenter
            text: consolePane.visibleHostCount
            visible: consolePane.visibleHostCount > 0
        }

        Item { Layout.fillWidth: true }

        // Discovery toggle
        AbstractButton {
            id: discoveryToggle
            Layout.alignment: Qt.AlignVCenter
            focusPolicy: Qt.NoFocus
            hoverEnabled: true
            checkable: true
            checked: Chiaki.discoveryEnabled
            onToggled: Chiaki.discoveryEnabled = !Chiaki.discoveryEnabled
            implicitWidth: discoveryRow.implicitWidth + 32
            implicitHeight: 40

            background: Rectangle {
                radius: height / 2
                color: discoveryToggle.checked ? C.Theme.successSoft : (discoveryToggle.hovered ? C.Theme.surfaceHover : C.Theme.surfaceRaised)
                border.width: 1
                border.color: discoveryToggle.checked ? "#4D34D399" : C.Theme.stroke
                Behavior on color { ColorAnimation { duration: C.Theme.durMed } }
            }

            contentItem: Item {
                Row {
                    id: discoveryRow
                    anchors.centerIn: parent
                    spacing: 10

                    Item {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 10
                        height: 10

                        Rectangle {
                            id: pulse
                            anchors.centerIn: parent
                            width: 10
                            height: 10
                            radius: 5
                            color: C.Theme.success
                            visible: discoveryToggle.checked

                            SequentialAnimation on scale {
                                running: discoveryToggle.checked && consolePane.visible && consolePane.StackView.status === StackView.Active
                                loops: Animation.Infinite
                                alwaysRunToEnd: true
                                NumberAnimation { from: 1.0; to: 2.4; duration: 1400; easing.type: Easing.OutCubic }
                                PauseAnimation { duration: 400 }
                            }
                            opacity: (2.4 - scale) / 2.0
                        }

                        Rectangle {
                            anchors.centerIn: parent
                            width: 10
                            height: 10
                            radius: 5
                            color: discoveryToggle.checked ? C.Theme.success : C.Theme.textTertiary
                        }
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: discoveryToggle.checked ? qsTr("Discovery on") : qsTr("Discovery off")
                        font.family: C.Theme.fontFamily
                        font.pixelSize: C.Theme.fontLabel
                        font.weight: Font.DemiBold
                        color: discoveryToggle.checked ? C.Theme.success : C.Theme.textSecondary
                    }
                }
            }
        }
    }

    // ── Console list ───────────────────────────────────────────────────────
    ListView {
        id: hostsView
        keyNavigationWraps: true
        anchors {
            top: sectionHeader.bottom
            left: parent.left
            right: parent.right
            bottom: footer.top
            topMargin: 6
        }
        leftMargin: 40
        rightMargin: 40
        bottomMargin: 8
        clip: true
        model: Chiaki.hosts
        highlightMoveDuration: C.Theme.durMed
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        onCountChanged: {
            if(!hostsView.currentItem)
                hostsView.incrementCurrentIndex();
            if(!hostsView.currentItem)
                return;
            if(!hostsView.currentItem.visible)
            {
                for(var i = 0; i < hostsView.count; i++)
                {
                    hostsView.incrementCurrentIndex()
                    if(hostsView.currentItem.visible)
                    {
                        break;
                    }
                }
            }
        }
        delegate: ItemDelegate {
            visible: modelData.display
            id: delegate
            width: hostsView.width - hostsView.leftMargin - hostsView.rightMargin
            // 8px above each card leaves room for the focus glow when the list scrolls to it
            height: modelData.display ? Math.max(184, detailsColumn.implicitHeight + 72) : 0
            highlighted: ListView.isCurrentItem
            hoverEnabled: true
            padding: 0
            onClicked: connectToHost()

            readonly property bool isPsn: !!modelData.duid
            readonly property bool isReady: modelData.state == "ready"
            readonly property bool isStandby: modelData.state == "standby"
            readonly property color stateTone: isPsn ? C.Theme.info : (isReady ? C.Theme.success : (isStandby ? C.Theme.warning : C.Theme.textTertiary))

            function connectToHost() {
                if(modelData.discovered)
                    Chiaki.connectToHost(index, modelData.name);
                else
                    Chiaki.connectToHost(index);
            }

            function wakeUpHost() {
                if(!modelData.discovered && !modelData.duid)
                    Chiaki.wakeUpHost(index);
            }

            function deleteHost() {
                if (modelData.manual)
                    root.showConfirmDialog(qsTr("Delete Console"), qsTr("Are you sure you want to delete this console?"), () => {Chiaki.deleteHost(index)});

                else if (modelData.discovered && !modelData.registered)
                    root.showConfirmDialog(qsTr("Hide Console"), qsTr("Are you sure you want to hide this console?") + "\n\n" + qsTr("Note: You can unhide from the Consoles section of the Settings under Hidden Consoles"), () => Chiaki.hideHost(modelData.mac, modelData.name));

            }

            function setConsolePin() {
                root.showConsolePinDialog(index);
            }

            background: Item {
                // Focus glow (static rectangles, no blur)
                Rectangle {
                    anchors.fill: card
                    anchors.margins: -6
                    radius: card.radius + 6
                    color: "#1F4D8DFF"
                    opacity: delegate.highlighted ? 1.0 : 0.0
                    Behavior on opacity { NumberAnimation { duration: C.Theme.durMed } }
                }

                Rectangle {
                    id: card
                    anchors {
                        fill: parent
                        topMargin: 8
                        bottomMargin: 14
                    }
                    radius: 24
                    color: delegate.highlighted ? C.Theme.surfaceRaised : (delegate.hovered ? "#171B27" : C.Theme.surface)
                    border.width: delegate.highlighted ? 2 : 1
                    border.color: delegate.highlighted ? C.Theme.accent : C.Theme.stroke
                    Behavior on color { ColorAnimation { duration: C.Theme.durMed } }

                    Rectangle {
                        anchors {
                            top: parent.top
                            left: parent.left
                            right: parent.right
                            margins: parent.border.width
                        }
                        height: parent.height * 0.6
                        radius: parent.radius
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: "#0AFFFFFF" }
                            GradientStop { position: 1.0; color: "transparent" }
                        }
                    }
                }
            }

            contentItem: Item {
                RowLayout {
                    anchors {
                        fill: parent
                        bottomMargin: 14
                        leftMargin: 14
                        rightMargin: 22
                        topMargin: 22
                    }
                    spacing: consolePane.compact ? 16 : 24

                    // Console art tile
                    Rectangle {
                        Layout.fillHeight: true
                        Layout.preferredWidth: consolePane.compact ? 136 : 232
                        radius: 16
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop {
                                position: 0.0
                                color: delegate.isPsn ? "#1A2C4A" : (delegate.isReady ? "#16264A" : (delegate.isStandby ? "#2A2212" : "#1A1D27"))
                            }
                            GradientStop {
                                position: 1.0
                                color: delegate.isPsn ? "#14203A" : (delegate.isReady ? "#24164A" : (delegate.isStandby ? "#1A1712" : "#141720"))
                            }
                        }
                        border.width: 1
                        border.color: C.Theme.stroke

                        Rectangle {
                            anchors {
                                left: parent.left
                                right: parent.right
                                bottom: parent.bottom
                                margins: 1
                            }
                            height: parent.height * 0.5
                            radius: parent.radius
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: "transparent" }
                                GradientStop { position: 1.0; color: Qt.rgba(delegate.stateTone.r, delegate.stateTone.g, delegate.stateTone.b, 0.10) }
                            }
                        }

                        // Soft spotlight so dark console art stays readable
                        Shape {
                            id: spotlight
                            anchors.fill: parent
                            preferredRendererType: Shape.GeometryRenderer

                            ShapePath {
                                strokeColor: "transparent"
                                fillGradient: RadialGradient {
                                    centerX: spotlight.width / 2
                                    centerY: spotlight.height / 2
                                    centerRadius: spotlight.width / 2
                                    focalX: centerX
                                    focalY: centerY
                                    GradientStop { position: 0.0; color: "#7A7C93C4" }
                                    GradientStop { position: 0.55; color: "#267C93C4" }
                                    GradientStop { position: 1.0; color: "#007C93C4" }
                                }
                                startX: 0; startY: 0
                                PathLine { x: spotlight.width; y: 0 }
                                PathLine { x: spotlight.width; y: spotlight.height }
                                PathLine { x: 0; y: spotlight.height }
                                PathLine { x: 0; y: 0 }
                            }
                        }

                        Image {
                            anchors {
                                fill: parent
                                margins: 18
                            }
                            fillMode: Image.PreserveAspectFit
                            source: "image://svg/console-ps" + (modelData.ps5 ? "5" : "4") + (modelData.state == "standby" ? "#light_standby" : "#light_on")
                            sourceSize: Qt.size(width, height)
                        }

                        Text {
                            anchors {
                                left: parent.left
                                top: parent.top
                                margins: 12
                            }
                            text: modelData.ps5 ? "PS5" : "PS4"
                            font.family: C.Theme.fontFamily
                            font.pixelSize: C.Theme.fontMicro
                            font.weight: Font.Bold
                            font.letterSpacing: 1.2
                            color: C.Theme.textTertiary
                        }
                    }

                    // Details
                    ColumnLayout {
                        id: detailsColumn
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 10

                        Text {
                            Layout.fillWidth: true
                            text: modelData.name ? modelData.name : (modelData.address ? modelData.address : qsTr("Unknown console"))
                            font.family: C.Theme.fontFamily
                            font.pixelSize: C.Theme.fontHeadline
                            font.weight: Font.Bold
                            font.letterSpacing: -0.3
                            color: C.Theme.text
                            elide: Text.ElideRight
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: 8

                            C.Chip {
                                dot: true
                                tinted: true
                                tone: delegate.stateTone
                                text: {
                                    if (delegate.isPsn)
                                        return modelData.discovered ? qsTr("Auto-registration available") : qsTr("Remote via PSN");
                                    if (delegate.isReady)
                                        return qsTr("Ready");
                                    if (delegate.isStandby)
                                        return qsTr("Rest mode");
                                    return modelData.state ? modelData.state.charAt(0).toUpperCase() + modelData.state.slice(1) : qsTr("Unknown");
                                }
                            }

                            C.Chip {
                                visible: !delegate.isPsn
                                tinted: modelData.registered
                                tone: C.Theme.accentBright
                                text: modelData.registered ? qsTr("Registered") : qsTr("Not registered")
                            }

                            C.Chip {
                                visible: !delegate.isPsn
                                text: {
                                    if (modelData.discovered)
                                        return modelData.manual ? qsTr("Discovered + manual") : qsTr("Discovered");
                                    return qsTr("Manual");
                                }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: text
                            text: {
                                let parts = [];
                                if (modelData.address)
                                    parts.push(qsTr("Address %1").arg(Chiaki.settings.streamerMode ? "hidden" : modelData.address));
                                if (modelData.mac)
                                    parts.push(qsTr("ID %1").arg(Chiaki.settings.streamerMode ? "hidden" : modelData.mac));
                                return parts.join("   ·   ");
                            }
                            font.family: C.Theme.fontFamily
                            font.pixelSize: C.Theme.fontCaption
                            color: C.Theme.textTertiary
                            elide: Text.ElideRight
                        }

                        Row {
                            Layout.fillWidth: true
                            visible: !delegate.isPsn && modelData.discovered && !!modelData.app
                            spacing: 8

                            Image {
                                anchors.verticalCenter: parent.verticalCenter
                                source: C.Theme.icon("play")
                                sourceSize: Qt.size(14, 14)
                                opacity: 0.8
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.min(implicitWidth, parent.parent.width - 30)
                                text: modelData.titleId ? qsTr("%1  (%2)").arg(modelData.app).arg(modelData.titleId) : (modelData.app || "")
                                font.family: C.Theme.fontFamily
                                font.pixelSize: C.Theme.fontCaption
                                font.weight: Font.Medium
                                color: C.Theme.textSecondary
                                elide: Text.ElideRight
                            }
                        }
                    }

                    // Actions
                    ColumnLayout {
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 10

                        C.PillButton {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.minimumWidth: 156
                            text: modelData.registered || delegate.isPsn ? qsTr("Connect") : qsTr("Register")
                            kind: delegate.highlighted ? "primary" : "secondary"
                            iconSource: delegate.highlighted && consolePane.hasController ? "" : C.Theme.icon("play")
                            hint: delegate.highlighted && consolePane.hasController ? root.controllerButton("cross") : ""
                            focusPolicy: Qt.NoFocus
                            onClicked: delegate.connectToHost()
                        }

                        Row {
                            Layout.alignment: Qt.AlignHCenter
                            spacing: 6

                            C.PillButton {
                                text: qsTr("Wake")
                                kind: "ghost"
                                compact: true
                                iconSource: delegate.highlighted && consolePane.hasController ? "" : C.Theme.icon("sun")
                                hint: delegate.highlighted && consolePane.hasController ? root.controllerButton("pyramid") : ""
                                visible: modelData.registered && !modelData.duid && !modelData.discovered
                                focusPolicy: Qt.NoFocus
                                onClicked: delegate.wakeUpHost()
                            }

                            C.PillButton {
                                text: qsTr("PIN")
                                kind: "ghost"
                                compact: true
                                iconSource: delegate.highlighted && consolePane.hasController ? "" : C.Theme.icon("pin")
                                hint: delegate.highlighted && consolePane.hasController ? "qrc:/icons/l1.svg" : ""
                                visible: modelData.registered
                                focusPolicy: Qt.NoFocus
                                onClicked: delegate.setConsolePin()
                            }

                            C.PillButton {
                                text: modelData.manual ? qsTr("Delete") : qsTr("Hide")
                                kind: "ghost"
                                compact: true
                                iconSource: delegate.highlighted && consolePane.hasController ? "" : C.Theme.icon(modelData.manual ? "trash" : "eye-off")
                                hint: delegate.highlighted && consolePane.hasController ? root.controllerButton("box") : ""
                                visible: modelData.manual || (modelData.discovered && !modelData.registered)
                                focusPolicy: Qt.NoFocus
                                onClicked: delegate.deleteHost()
                            }
                        }
                    }
                }
            }
        }
    }

    // Soft edges when the list scrolls (static gradients, only shown while there is more to scroll)
    Rectangle {
        anchors {
            left: hostsView.left
            right: hostsView.right
            top: hostsView.top
        }
        height: 24
        visible: hostsView.contentY > hostsView.originY + 1
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#E60C0F17" }
            GradientStop { position: 1.0; color: "#000C0F17" }
        }
    }

    Rectangle {
        anchors {
            left: hostsView.left
            right: hostsView.right
            bottom: hostsView.bottom
        }
        height: 40
        visible: hostsView.contentY + hostsView.height < hostsView.originY + hostsView.contentHeight + hostsView.bottomMargin - 1
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#000A0C12" }
            GradientStop { position: 1.0; color: "#F20A0C12" }
        }
    }

    // ── Empty state ────────────────────────────────────────────────────────
    ColumnLayout {
        anchors.centerIn: hostsView
        visible: consolePane.visibleHostCount === 0
        spacing: 18

        Item {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 180
            Layout.preferredHeight: 180

            Repeater {
                model: 3
                Rectangle {
                    anchors.centerIn: parent
                    width: 72 + index * 48
                    height: width
                    radius: width / 2
                    color: "transparent"
                    border.width: 1
                    border.color: Qt.rgba(0.30, 0.55, 1.0, 0.22 - index * 0.06)
                }
            }

            Rectangle {
                id: radarPing
                anchors.centerIn: parent
                width: 72
                height: 72
                radius: width / 2
                color: "transparent"
                border.width: 2
                border.color: C.Theme.accent
                visible: Chiaki.discoveryEnabled

                ParallelAnimation {
                    running: radarPing.visible && consolePane.visibleHostCount === 0 && consolePane.StackView.status === StackView.Active
                    loops: Animation.Infinite
                    NumberAnimation { target: radarPing; property: "scale"; from: 1.0; to: 2.5; duration: 2200; easing.type: Easing.OutCubic }
                    NumberAnimation { target: radarPing; property: "opacity"; from: 0.8; to: 0.0; duration: 2200; easing.type: Easing.OutCubic }
                }
            }

            Rectangle {
                anchors.centerIn: parent
                width: 72
                height: 72
                radius: 24
                gradient: Gradient {
                    GradientStop { position: 0.0; color: C.Theme.accent }
                    GradientStop { position: 1.0; color: C.Theme.accentAlt }
                }

                Image {
                    anchors.centerIn: parent
                    source: C.Theme.icon(Chiaki.discoveryEnabled ? "radar" : "radar-off")
                    sourceSize: Qt.size(34, 34)
                }
            }
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: Chiaki.discoveryEnabled ? qsTr("Looking for consoles…") : qsTr("Discovery is off")
            font.family: C.Theme.fontFamily
            font.pixelSize: C.Theme.fontHeadline
            font.weight: Font.Bold
            color: C.Theme.text
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: 560
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: Chiaki.discoveryEnabled
                  ? qsTr("Make sure your PlayStation is on and connected to the same network, or add it manually by IP address.")
                  : qsTr("Turn on discovery to find PlayStation consoles on your network, or add one manually by IP address.")
            font.family: C.Theme.fontFamily
            font.pixelSize: C.Theme.fontLabel
            color: C.Theme.textSecondary
            lineHeight: 1.2
        }

        Row {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 8
            spacing: 12

            C.PillButton {
                text: qsTr("Turn on discovery")
                iconSource: C.Theme.icon("radar")
                visible: !Chiaki.discoveryEnabled
                focusPolicy: Qt.NoFocus
                onClicked: Chiaki.discoveryEnabled = true
            }

            C.PillButton {
                text: qsTr("Add console manually")
                kind: "primary"
                iconSource: C.Theme.icon("plus")
                focusPolicy: Qt.NoFocus
                onClicked: root.showManualHostDialog()
            }
        }
    }

    // ── Footer: input hints + version ──────────────────────────────────────
    Item {
        id: footer
        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        height: 64

        Rectangle {
            anchors {
                top: parent.top
                left: parent.left
                right: parent.right
                leftMargin: 40
                rightMargin: 40
            }
            height: 1
            color: C.Theme.stroke
        }

        Row {
            anchors {
                left: parent.left
                verticalCenter: parent.verticalCenter
                leftMargin: 40
            }
            spacing: 24
            visible: consolePane.hasController && width + 160 < footer.width

            C.KeyHint { glyph: root.controllerButton("cross"); text: qsTr("Connect") }
            C.KeyHint { glyph: root.controllerButton("pyramid"); text: qsTr("Wake") }
            C.KeyHint { glyph: root.controllerButton("box"); text: qsTr("Hide / Delete") }
            C.KeyHint { glyph: "qrc:/icons/l1.svg"; text: qsTr("PIN") }
            C.KeyHint { glyph: "qrc:/icons/r3.svg"; text: qsTr("Add Console") }
        }

        Row {
            anchors {
                left: parent.left
                verticalCenter: parent.verticalCenter
                leftMargin: 40
            }
            spacing: 24
            visible: !consolePane.hasController && width + 160 < footer.width

            C.KeyHint { keyText: "↑↓"; text: qsTr("Select") }
            C.KeyHint { keyText: "Enter"; text: qsTr("Connect") }
            C.KeyHint { keyText: "F2"; text: qsTr("Add Console") }
            C.KeyHint { keyText: "PgUp"; text: qsTr("PIN") }
            C.KeyHint { keyText: "Esc"; text: qsTr("Quit") }
        }

        Text {
            anchors {
                right: parent.right
                verticalCenter: parent.verticalCenter
                rightMargin: 40
            }
            text: "v" + Qt.application.version
            font.family: C.Theme.fontFamily
            font.pixelSize: C.Theme.fontCaption
            font.weight: Font.Medium
            color: C.Theme.textTertiary
        }
    }
}
