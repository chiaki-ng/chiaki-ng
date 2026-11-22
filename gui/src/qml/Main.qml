import QtCore
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

import org.streetpea.chiaking

Item {
    id: root
    property list<Item> restoreFocusItems
    property bool initialAsk: false
    readonly property bool preferSeparateStreamSettingsWindows: Chiaki.window.runtimeRendererBackend === 1 && Chiaki.session
    property bool useSeparateStreamSettingsWindows: false
    // Temas inspirados en PlayStation y sus juegos; se elige en Ajustes > General
    readonly property var themes: ({
        "chiaki":   { name: qsTr("chiaki-ng (original)"), accent: "#00a7ff", background: "#303030", primary: "#00a7ff" },
        "ps5":      { name: qsTr("PS5 clásico"),          accent: "#53b5ff", background: "#0b0d12", primary: "#17243a" },
        "ps4":      { name: qsTr("PS4"),                  accent: "#00a7ff", background: "#0a1f44", primary: "#2e6db4" },
        "gow":      { name: qsTr("God of War"),           accent: "#ee5266", background: "#160909", primary: "#39232a" },
        "spiderman":{ name: qsTr("Marvel's Spider-Man"),  accent: "#e23636", background: "#0d0f1f", primary: "#1f4fa3" },
        "horizon":  { name: qsTr("Horizon"),              accent: "#f5a450", background: "#0b1a1c", primary: "#163c45" },
        "tlou":     { name: qsTr("The Last of Us"),       accent: "#b4ca7d", background: "#111410", primary: "#2b3726" },
        "ghost":    { name: qsTr("Ghost of Tsushima"),    accent: "#f47971", background: "#141210", primary: "#382a28" },
        "astro":    { name: qsTr("Astro Bot"),            accent: "#63c9ff", background: "#0c1426", primary: "#1c3459" },
        "gt7":      { name: "Gran Turismo",              accent: "#62b6ff", background: "#0c1018", primary: "#1b2c45" },
        "ratchet":  { name: "Ratchet & Clank",           accent: "#ffb758", background: "#13102b", primary: "#34244c" },
        "returnal": { name: "Returnal",                  accent: "#89e6ce", background: "#07191c", primary: "#153338" },
        "uncharted":{ name: "Uncharted",                 accent: "#e8c17e", background: "#18150c", primary: "#3a3020" },
        "bloodborne":{name: "Bloodborne",                accent: "#df9b9c", background: "#100e18", primary: "#302332" },
        "demons":   { name: "Demon's Souls",             accent: "#9bc5e8", background: "#0b131c", primary: "#203141" },
        "stranding":{ name: "Death Stranding",           accent: "#a4d9e8", background: "#10181d", primary: "#273941" }
    })
    readonly property var themeKeys: ["ps5", "ps4", "gow", "spiderman", "horizon", "tlou", "ghost", "astro", "gt7", "ratchet", "returnal", "uncharted", "bloodborne", "demons", "stranding", "chiaki"]
    Settings {
        id: themeSettings
        category: "theme"
        property string current: "ps5"
    }
    readonly property string currentThemeKey: themes[themeSettings.current] ? themeSettings.current : "ps5"
    readonly property var currentTheme: themes[currentThemeKey]
    function setTheme(key) { if (themes[key]) themeSettings.current = key; }

    Material.theme: Material.Dark
    Material.accent: currentTheme.accent
    Material.primary: currentTheme.primary
    Material.background: currentTheme.background

    function controllerButton(name) {
        let type = "deck";
        for (let i = 0; i < Chiaki.controllers.length; ++i) {
            if (Chiaki.controllers[i].playStation) {
                type = "ps";
                break;
            }
        }
        return "image://svg/button-%1#%2".arg(type).arg(name);
    }
    function grabInput(item) {
        Chiaki.window.grabInput();
        restoreFocusItems.push(Window.window.activeFocusItem);
        if (item)
            item.forceActiveFocus(Qt.TabFocusReason);
    }

    function releaseInput() {
        Chiaki.window.releaseInput();
        let item = restoreFocusItems.pop();
        if (item && item.visible)
            item.forceActiveFocus(Qt.TabFocusReason);
    }

    function autoRegister(auto, host, ps5) {
        if(auto)
            Chiaki.autoRegister()
        else
            showRegistDialog(host, ps5);
    }

    function openDisplaySettings() {
        useSeparateStreamSettingsWindows = preferSeparateStreamSettingsWindows;
        const loader = useSeparateStreamSettingsWindows ? separateDisplaySettingsLoader : displaySettingsLoader;
        if(loader.status == Loader.Ready)
        {
            if(placeboSettingsRect.opacity || displaySettingsRect.opacity || colorMappingSettingsRect.opacity)
                closeDialog();
            displaySettingsRect.opacity = 0.8;
            grabInput(loader);
            if (!loader.item.restoreFocusItem) {
                let item = loader.item.mainItem.nextItemInFocusChain();
                if (item)
                    item.forceActiveFocus(Qt.TabFocusReason);
            } else {
                loader.item.restoreFocusItem.forceActiveFocus(Qt.TabFocusReason);
                loader.item.restoreFocusItem = null;
            }
        }
    }
    function openPlaceboSettings() {
        if (!displaySettingsRect.opacity && !placeboSettingsRect.opacity && !colorMappingSettingsRect.opacity)
            useSeparateStreamSettingsWindows = preferSeparateStreamSettingsWindows;
        const loader = useSeparateStreamSettingsWindows ? separatePlaceboSettingsLoader : placeboSettingsLoader;
        if(loader.status == Loader.Ready)
        {
            if(placeboSettingsRect.opacity || displaySettingsRect.opacity || colorMappingSettingsRect.opacity)
                closeDialog();
            placeboSettingsRect.opacity = 0.8;
            grabInput(loader);
            if (!loader.item.restoreFocusItem) {
                let item = loader.item.mainItem.nextItemInFocusChain();
                if (item)
                    item.forceActiveFocus(Qt.TabFocusReason);
            } else {
                loader.item.restoreFocusItem.forceActiveFocus(Qt.TabFocusReason);
                loader.item.restoreFocusItem = null;
            }
        }
    }

    function closeDialog() {
       if(displaySettingsRect.opacity) {
        displaySettingsRect.opacity = 0.0;
        const loader = useSeparateStreamSettingsWindows ? separateDisplaySettingsLoader : displaySettingsLoader;
        loader.item.restoreFocusItem = Window.window.activeFocusItem;
        releaseInput();
        useSeparateStreamSettingsWindows = false;
       }
       else if(placeboSettingsRect.opacity) {
        placeboSettingsRect.opacity = 0.0;
        const loader = useSeparateStreamSettingsWindows ? separatePlaceboSettingsLoader : placeboSettingsLoader;
        loader.item.restoreFocusItem = Window.window.activeFocusItem;
        releaseInput();
        useSeparateStreamSettingsWindows = false;
       }
       else if(colorMappingSettingsRect.opacity) {
        releaseInput();
        colorMappingSettingsRect.opacity = 0.0;
        const loader = useSeparateStreamSettingsWindows ? separateColorMappingSettingsLoader : colorMappingSettingsLoader;
        loader.item.restoreFocusItem = Window.window.activeFocusItem;
        root.openPlaceboSettings();
       }
       else
        stack.pop();
    }

    function showMainView() {
        if(displaySettingsRect.opacity) {
            displaySettingsRect.opacity = 0.0;
            const loader = useSeparateStreamSettingsWindows ? separateDisplaySettingsLoader : displaySettingsLoader;
            loader.item.restoreFocusItem = Window.window.activeFocusItem;
            releaseInput();
            useSeparateStreamSettingsWindows = false;
        }
        else if(placeboSettingsRect.opacity) {
            placeboSettingsRect.opacity = 0.0;
            const loader = useSeparateStreamSettingsWindows ? separatePlaceboSettingsLoader : placeboSettingsLoader;
            loader.item.restoreFocusItem = Window.window.activeFocusItem;
            releaseInput();
            useSeparateStreamSettingsWindows = false;
        }
        else if(colorMappingSettingsRect.opacity) {
            colorMappingSettingsRect.opacity = 0.0;
            const loader = useSeparateStreamSettingsWindows ? separateColorMappingSettingsLoader : colorMappingSettingsLoader;
            loader.item.restoreFocusItem = Window.window.activeFocusItem;
            releaseInput();
            useSeparateStreamSettingsWindows = false;
        }
        if (stack.depth > 1)
            stack.pop(stack.get(0));
        else
            stack.replace(stack.get(0), mainViewComponent);
    }

    function showStreamView() {
        stack.replace(stack.get(0), streamViewComponent);
    }

    function showPsnView() {
        stack.replace(stack.get(0), psnViewComponent, {}, StackView.Immediate)
    }

    function showManualHostDialog() {
        stack.push(manualHostDialogComponent);
    }

    function showConfirmDialog(title, text, callback, rejectCallback = null) {
        confirmDialog.title = title;
        confirmDialog.text = text;
        confirmDialog.callback = callback;
        confirmDialog.rejectCallback = rejectCallback;
        confirmDialog.restoreFocusItem = Window.window.activeFocusItem;
        confirmDialog.open();
    }

    function showInfoDialog(title, text) {
        infoDialog.title = title;
        infoDialog.text = text;
        infoDialog.restoreFocusItem = Window.window.activeFocusItem;
        infoDialog.open();
    }

    function showRendererFallbackDialog(reason) {
        showInfoDialog(qsTr("Renderer Fallback"),
                       qsTr("Vulkan renderer is unavailable and chiaki-ng switched to OpenGL.\n\nReason: %1").arg(reason));
    }

    function showRemindDialog(title, text, remotePlay, callback) {
        remindDialog.title = title;
        remindDialog.text = text;
        remindDialog.remotePlay = remotePlay;
        remindDialog.callback = callback;
        remindDialog.restoreFocusItem = Window.window.activeFocusItem;
        remindDialog.open();
    }


    function showRegistDialog(host, ps5) {
        stack.push(registDialogComponent, {host: host, ps5: ps5});
    }

    function showSettingsDialog() {
        stack.push(settingsDialogComponent);
    }

    function showDisplaySettingsDialog() {
        stack.push(displaySettingsDialogComponent);
    }

    function showPlaceboSettingsDialog() {
        stack.push(placeboSettingsDialogComponent);
    }

    function showPlaceboColorMappingDialog() {
        if(placeboSettingsRect.opacity)
        {
            root.closeDialog();
            useSeparateStreamSettingsWindows = preferSeparateStreamSettingsWindows;
            const loader = useSeparateStreamSettingsWindows ? separateColorMappingSettingsLoader : colorMappingSettingsLoader;
            if(loader.status == Loader.Ready)
            {
                grabInput(loader);
                colorMappingSettingsRect.opacity = 0.8;
                if (!loader.item.restoreFocusItem) {
                    let item = loader.item.mainItem.nextItemInFocusChain();
                    if (item)
                        item.forceActiveFocus(Qt.TabFocusReason);
                } else {
                    loader.item.restoreFocusItem.forceActiveFocus(Qt.TabFocusReason);
                    loader.item.restoreFocusItem = null;
                }
            }
        }
        else
            stack.push(placeboColorMappingDialogComponent);
    }

    function showProfileDialog() {
        stack.push(profileDialogComponent)
    }

    function showConsolePinDialog(consoleIndex) {
        stack.push(consolePinDialogComponent, {consoleIndex: consoleIndex});
    }

    function showSteamShortcutDialog(fromReminder) {
        stack.push(steamShortcutDialogComponent, {fromReminder: fromReminder});
    }

    function showPSNTokenDialog(psnurl, expired) {
        stack.push(psnTokenDialogComponent, {psnurl: psnurl, expired: expired});
    }

    function showControllerMappingDialog() {
        stack.push(controllerMappingDialogComponent)
    }

    Component.onCompleted: {
        if (Chiaki.session)
            stack.replace(stack.get(0), streamViewComponent, {}, StackView.Immediate);
        else if (Chiaki.autoConnect)
            stack.replace(stack.get(0), autoConnectViewComponent, {}, StackView.Immediate);
    }

    Pane {
        anchors.fill: parent
        visible: !Chiaki.window.hasVideo && !Chiaki.window.keepVideo
    }

    StackView {
        id: stack
        anchors.fill: parent
        hoverEnabled: false
        initialItem: mainViewComponent
        font.pixelSize: 20

        replaceEnter: Transition {
            PropertyAnimation {
                property: "opacity"
                from: 0.01
                to: 1.0
                duration: 200
            }
        }

        replaceExit: Transition {
            PropertyAnimation {
                property: "opacity"
                from: 1.0
                to: 0.0
                duration: 200
            }
        }
    }

    Rectangle {
        id: placeboSettingsRect
        opacity: 0.0
        visible: opacity && !useSeparateStreamSettingsWindows
        height: 650
        width: 1200
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        color: Material.background
        Loader {
            anchors.fill: parent
            id: placeboSettingsLoader
            sourceComponent: placeboSettingsDialogComponent
        }
    }
    Rectangle {
        id: displaySettingsRect
        opacity: 0.0
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        height: 500
        width: 1200
        visible: opacity && !useSeparateStreamSettingsWindows
        color: Material.background
        Loader {
            anchors.fill: parent
            id: displaySettingsLoader
            sourceComponent: displaySettingsDialogComponent
        }
    }
    Rectangle {
        id: colorMappingSettingsRect
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        opacity: 0.0
        height: 600
        width: 1200
        visible: opacity && !useSeparateStreamSettingsWindows
        color: Material.background
        Loader {
            anchors.fill: parent
            id: colorMappingSettingsLoader
            sourceComponent: placeboColorMappingDialogComponent
        }
    }

    Window {
        id: separateDisplaySettingsWindow
        visible: useSeparateStreamSettingsWindows && displaySettingsRect.opacity > 0.0
        transientParent: Chiaki.window
        flags: Qt.Dialog | Qt.FramelessWindowHint
        color: Material.background
        modality: Qt.NonModal
        x: Math.round(root.mapToGlobal(0, 0).x)
        y: Math.round(root.mapToGlobal(0, 0).y)
        width: Math.round(root.width)
        height: separateDisplaySettingsLoader.item ? Math.min(Math.round(root.height), Math.round(separateDisplaySettingsLoader.item.gridHeight + 140)) : 500
        onVisibleChanged: if (visible) requestActivate()

        Shortcut {
            sequence: "Ctrl+O"
            onActivated: root.closeDialog()
        }

        Shortcut {
            sequence: StandardKey.Cancel
            onActivated: root.closeDialog()
        }

        Loader {
            anchors.fill: parent
            id: separateDisplaySettingsLoader
            sourceComponent: displaySettingsDialogComponent
        }
    }

    Window {
        id: separatePlaceboSettingsWindow
        visible: useSeparateStreamSettingsWindows && placeboSettingsRect.opacity > 0.0
        transientParent: Chiaki.window
        flags: Qt.Dialog | Qt.FramelessWindowHint
        color: Material.background
        modality: Qt.NonModal
        x: Math.round(root.mapToGlobal(0, 0).x)
        y: Math.round(root.mapToGlobal(0, 0).y)
        width: Math.round(root.width)
        height: Math.min(Math.round(root.height), 650)
        onVisibleChanged: if (visible) requestActivate()

        Shortcut {
            sequence: "Ctrl+O"
            onActivated: root.closeDialog()
        }

        Shortcut {
            sequence: StandardKey.Cancel
            onActivated: root.closeDialog()
        }

        Loader {
            anchors.fill: parent
            id: separatePlaceboSettingsLoader
            sourceComponent: placeboSettingsDialogComponent
        }
    }

    Window {
        id: separateColorMappingSettingsWindow
        visible: useSeparateStreamSettingsWindows && colorMappingSettingsRect.opacity > 0.0
        transientParent: Chiaki.window
        flags: Qt.Dialog | Qt.FramelessWindowHint
        color: Material.background
        modality: Qt.NonModal
        x: Math.round(root.mapToGlobal(0, 0).x)
        y: Math.round(root.mapToGlobal(0, 0).y)
        width: Math.round(root.width)
        height: Math.min(Math.round(root.height), 600)
        onVisibleChanged: if (visible) requestActivate()

        Shortcut {
            sequence: "Ctrl+O"
            onActivated: root.closeDialog()
        }

        Shortcut {
            sequence: StandardKey.Cancel
            onActivated: root.closeDialog()
        }

        Loader {
            anchors.fill: parent
            id: separateColorMappingSettingsLoader
            sourceComponent: placeboColorMappingDialogComponent
        }
    }
    Rectangle {
        anchors {
            bottom: parent.bottom
            horizontalCenter: parent.horizontalCenter
            bottomMargin: 30
        }
        color: Material.accent
        width: errorLayout.width + 40
        height: errorLayout.height + 20
        radius: 8
        opacity: errorHideTimer.running ? 0.8 : 0.0

        Behavior on opacity { NumberAnimation { duration: 500 } }

        ColumnLayout {
            id: errorLayout
            anchors.centerIn: parent

            Label {
                id: errorTitleLabel
                Layout.alignment: Qt.AlignCenter
                font.bold: true
                font.pixelSize: 24
            }

            Label {
                id: errorTextLabel
                Layout.alignment: Qt.AlignCenter
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: 20
            }
        }

        Timer {
            id: errorHideTimer
            interval: 2000
        }
    }

    Dialog {
        id: infoDialog
        property alias text: infoDialogLabel.text
        property Item restoreFocusItem
        parent: Overlay.overlay
        x: Math.round((root.width - width) / 2)
        y: Math.round((root.height - height) / 2)
        modal: true
        Material.roundedScale: Material.MediumScale
        onOpened: infoDialogLabel.forceActiveFocus(Qt.TabFocusReason)
        onClosed: {
            if (restoreFocusItem)
                restoreFocusItem.forceActiveFocus(Qt.TabFocusReason);
            infoDialogLabel.focus = false;
        }

        Component.onCompleted: {
            header.horizontalAlignment = Text.AlignHCenter;
            header.background = null;
        }

        ColumnLayout {
            spacing: 20

            Label {
                id: infoDialogLabel
                Keys.onEscapePressed: infoDialog.close()
                Keys.onReturnPressed: infoDialog.close()
            }

            RowLayout {
                Layout.alignment: Qt.AlignCenter

                Button {
                    text: qsTr("OK")
                    Material.background: Material.accent
                    flat: true
                    leftPadding: 50
                    onClicked: infoDialog.close()
                    Material.roundedScale: Material.SmallScale

                    Image {
                        anchors {
                            left: parent.left
                            verticalCenter: parent.verticalCenter
                            leftMargin: 12
                        }
                        width: 28
                        height: 28
                        sourceSize: Qt.size(width, height)
                        source: root.controllerButton("cross")
                    }
                }
            }
        }
    }

    ConfirmDialog {
        id: confirmDialog
    }

    RemindDialog {
        id: remindDialog
    }

    Connections {
        target: Chiaki

        function onSessionChanged() {
            if (Chiaki.session)
                root.showStreamView();
        }

        function onShowPsnView() {
            root.showPsnView();
        }

        function onPsnCredsExpired() {
            Chiaki.settings.psnRefreshToken = ""
            Chiaki.settings.psnAuthToken = ""
            Chiaki.settings.psnAuthTokenExpiry = ""
            Chiaki.settings.psnAccountId = ""
            root.showPSNTokenDialog(true);
        }

        function onError(title, text) {
            errorTitleLabel.text = title;
            errorTextLabel.text = text;
            errorHideTimer.start();
        }

        function onRegistDialogRequested(host, ps5, duid) {
            if(!duid)
                showRegistDialog(host, ps5);
            else
            {
                if(ps5)
                    root.showConfirmDialog(qsTr("Registration Type"), qsTr("Would you like to use automatic registration?"), () => root.autoRegister(true, host, ps5), () => root.autoRegister(false, host, ps5))
                else
                    root.showConfirmDialog(qsTr("Registration Type"), qsTr("Would you like to use automatic registration (must be main PS4 console registered to your PSN)?"), () => root.autoRegister(true, host, ps5), () => root.autoRegister(false, host, ps5))
            }
        }

        function onWakeupStartInitiated() {
            stack.replace(stack.get(0), autoConnectViewComponent, {}, StackView.Immediate);
        }
    }

    Component {
        id: mainViewComponent
        MainView { }
    }

    Component {
        id: streamViewComponent
        StreamView { }
    }

    Component {
        id: autoConnectViewComponent
        AutoConnectView { }
    }

    Component {
        id: psnViewComponent
        PsnView {}
    }

    Component {
        id: manualHostDialogComponent
        ManualHostDialog { }
    }

    Component {
        id: settingsDialogComponent
        SettingsDialog { }
    }

    Component {
        id: displaySettingsDialogComponent
        DisplaySettingsDialog { }
    }

    Component {
        id: placeboSettingsDialogComponent
        PlaceboSettingsDialog { }
    }

    Component {
        id: placeboColorMappingDialogComponent
        PlaceboColorMappingDialog { }
    }

    Component {
        id: profileDialogComponent
        ProfileDialog { }
    }

    Component {
        id: consolePinDialogComponent
        ConsolePinDialog { }
    }

    Component {
        id: steamShortcutDialogComponent
        SteamShortcutDialog { }
    }

    Component {
        id: psnTokenDialogComponent
        PSNTokenDialog { }
    }

    Component {
        id: registDialogComponent
        RegistDialog { }
    }

    Component {
        id: controllerMappingDialogComponent
        ControllerMappingDialog { }
    }
}
