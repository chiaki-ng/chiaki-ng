import QtCore
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs

import org.streetpea.chiaking

import "controls" as C

DialogView {
    id: dialog
    property string options_url: ""
    title: qsTr("Placebo Renderer Settings")
    header: qsTr("* Defaults in () to right of value or marked with (Default)")
    buttonVisible: false
    Keys.onPressed: (event) => {
        if (event.modifiers)
            return;
        switch (event.key) {
        case Qt.Key_PageUp:
            bar.decrementCurrentIndex();
            event.accepted = true;
            break;
        case Qt.Key_PageDown:
            bar.incrementCurrentIndex();
            event.accepted = true;
            break;
        }
    }

    Item {
        C.TabBar {
            id: bar
            anchors {
                top: parent.top
                left: parent.left
                right: parent.right
                topMargin: 16
                leftMargin: 24
                rightMargin: 24
            }

            C.TabButton {
                text: qsTr("Config")
                focusPolicy: Qt.NoFocus
            }

            C.TabButton {
                text: qsTr("Scaling")
                focusPolicy: Qt.NoFocus
            }

            C.TabButton {
                text: qsTr("Deinterlace")
                focusPolicy: Qt.NoFocus
            }

            C.TabButton {
                text: qsTr("Debanding")
                focusPolicy: Qt.NoFocus
            }

            C.TabButton {
                text: qsTr("Sigmoidization")
                focusPolicy: Qt.NoFocus
            }

            C.TabButton {
                text: qsTr("Color")
                focusPolicy: Qt.NoFocus
            }

            C.TabButton {
                text: qsTr("Peak Detection")
                focusPolicy: Qt.NoFocus
            }

            C.TabButton {
                text: qsTr("Color Mapping")
                focusPolicy: Qt.NoFocus
            }
        }

        StackLayout {
            anchors {
                top: bar.bottom
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }
            currentIndex: bar.currentIndex
            onCurrentIndexChanged: nextItemInFocusChain().forceActiveFocus(Qt.TabFocusReason)

            Item {
                // Config
                GridLayout {
                    anchors {
                        top: parent.top
                        horizontalCenter: parent.horizontalCenter
                        topMargin: 20
                    }
                    columns: 2
                    rowSpacing: 10
                    columnSpacing: 20

                    Label {
                        text: qsTr("Libplacebo Renderer Options Details")
                        visible: !options_url
                    }

                    C.Button {
                        id: openwebpage
                        text: qsTr("Open Documentation")
                        onClicked: {
                            options_url = Chiaki.openPlaceboOptionsLink()
                        }
                        visible: !options_url
                    }

                    Label {
                        text: qsTr("Open Documentation in Web Browser with copied URL")
                        visible: options_url
                    }

                    TextField {
                        id: openurl
                        echoMode: Chiaki.settings.streamerMode ? TextInput.Password : TextInput.Normal
                        text: options_url
                        visible: false
                        Layout.preferredWidth: 400
                    }

                    C.Button {
                        id: copyUrl
                        text: qsTr("Click to Re-Copy URL")
                        onClicked: {
                            openurl.selectAll()
                            openurl.copy()
                        }
                        visible: options_url
                    }

                    Label {
                        text: qsTr("Export Render Settings to File")
                    }

                    C.Button {
                        id: exportButton
                        text: qsTr("Export")
                        onClicked: {
                            Chiaki.settings.exportPlaceboSettings();
                        }
                        Material.roundedScale: Material.SmallScale
                        KeyNavigation.down: importButton
                        KeyNavigation.right: importButton
                    }

                    Label {
                        text: qsTr("Import Render Settings from File")
                    }

                    C.Button {
                        id: importButton
                        lastInFocusChain: true
                        text: qsTr("Import")
                        onClicked: {
                            Chiaki.settings.importPlaceboSettings();
                        }
                        Material.roundedScale: Material.SmallScale
                        KeyNavigation.up: exportButton
                        KeyNavigation.left: exportButton
                    }
                }
            }

            Item {
                // Scaling
                GridLayout {
                    anchors {
                        top: parent.top
                        horizontalCenter: parent.horizontalCenter
                        topMargin: 20
                    }
                    columns: 3
                    rowSpacing: 10
                    columnSpacing: 20

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Upscaler:")
                    }

                    C.ComboBox {
                        Layout.preferredWidth: 400
                        firstInFocusChain: true
                        model: [qsTr("Custom"), qsTr("Nearest"), qsTr("Bilinear"), qsTr("Oversample"), qsTr("Bicubic"), qsTr("Gaussian"), qsTr("Catmull Rom"), qsTr("Lanczos"), qsTr("EwaLanczos"), qsTr("EwaLanczosSharp"), qsTr("EwaLanczosSharpest"), qsTr("FSR"), qsTr("FSRCNNX x2 8-0-4-1"), qsTr("FSRCNNX x2 16-0-4-1")]
                        currentIndex: Chiaki.settings.placeboUpscaler
                        onActivated: index => Chiaki.settings.placeboUpscaler = index
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(EwaLanczos)")
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Plane Upscaler:")
                    }

                    C.ComboBox {
                        Layout.preferredWidth: 400
                        firstInFocusChain: true
                        model: [qsTr("Chosen Upscaler"), qsTr("Nearest"), qsTr("Bilinear"), qsTr("Oversample"), qsTr("Bicubic"), qsTr("Gaussian"), qsTr("Catmull Rom"), qsTr("Lanczos"), qsTr("EwaLanczos"), qsTr("EwaLanczosSharp"), qsTr("EwaLanczosSharpest")]
                        currentIndex: Chiaki.settings.placeboPlaneUpscaler
                        onActivated: index => Chiaki.settings.placeboPlaneUpscaler = index
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(Chosen Upscaler)")
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Downscaler:")
                    }

                    C.ComboBox {
                        Layout.preferredWidth: 400
                        firstInFocusChain: true
                        model: [qsTr("Custom"), qsTr("Box"), qsTr("Hermite"), qsTr("Bilinear"), qsTr("Bicubic"), qsTr("Gaussian"), qsTr("Catmull Rom"), qsTr("Mitchell"), qsTr("Lanczos")]
                        currentIndex: Chiaki.settings.placeboDownscaler
                        onActivated: index => Chiaki.settings.placeboDownscaler = index
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(Hermite)")
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Plane Downscaler:")
                    }

                    C.ComboBox {
                        Layout.preferredWidth: 400
                        firstInFocusChain: true
                        model: [qsTr("Chosen Downscaler"), qsTr("Box"), qsTr("Hermite"), qsTr("Bilinear"), qsTr("Bicubic"), qsTr("Gaussian"), qsTr("Catmull Rom"), qsTr("Mitchell"), qsTr("Lanczos")]
                        currentIndex: Chiaki.settings.placeboPlaneDownscaler
                        onActivated: index => Chiaki.settings.placeboPlaneDownscaler = index
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(Chosen Downscaler)")
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Anti-ringing Strength:")
                    }

                    C.Slider {
                        Layout.preferredWidth: 250
                        from: 0.00
                        to: 1.00
                        stepSize: 0.01
                        value: Chiaki.settings.placeboAntiringingStrength
                        onMoved: Chiaki.settings.placeboAntiringingStrength = value

                        Label {
                            anchors {
                                left: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 10
                            }
                            text: qsTr(parent.value.toFixed(2))
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(0.00)")
                    }
                }
            }

            Item {
                // Deinterlace
                GridLayout {
                    anchors {
                        top: parent.top
                        horizontalCenter: parent.horizontalCenter
                        topMargin: 20
                    }
                    columns: 3
                    rowSpacing: 10
                    columnSpacing: 20

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Enable Deinterlace:")
                    }

                    C.CheckBox {
                        checked: Chiaki.settings.placeboDeinterlaceEnabled
                        onToggled: Chiaki.settings.placeboDeinterlaceEnabled = checked
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(no)")
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Preset:")
                        visible: Chiaki.settings.placeboDeinterlaceEnabled
                    }

                    C.ComboBox {
                        Layout.preferredWidth: 400
                        model: [qsTr("Default")]
                        currentIndex: Chiaki.settings.placeboDeinterlacePreset
                        enabled: Chiaki.settings.placeboDeinterlaceEnabled
                        visible: Chiaki.settings.placeboDeinterlaceEnabled
                        onActivated: index => Chiaki.settings.placeboDeinterlacePreset = index
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(Default)")
                        visible: Chiaki.settings.placeboDeinterlaceEnabled
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Algorithm:")
                        visible: Chiaki.settings.placeboDeinterlaceEnabled
                    }

                    C.ComboBox {
                        Layout.preferredWidth: 400
                        model: [qsTr("Weave"), qsTr("Bob"), qsTr("Yadif"), qsTr("Bwdif")]
                        currentIndex: Chiaki.settings.placeboDeinterlaceAlgorithm
                        enabled: Chiaki.settings.placeboDeinterlaceEnabled
                        visible: Chiaki.settings.placeboDeinterlaceEnabled
                        onActivated: index => Chiaki.settings.placeboDeinterlaceAlgorithm = index
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(Yadif)")
                        visible: Chiaki.settings.placeboDeinterlaceEnabled
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Skip spatial check:")
                        visible: Chiaki.settings.placeboDeinterlaceEnabled
                    }

                    C.CheckBox {
                        checked: Chiaki.settings.placeboDeinterlaceSkipSpatial
                        enabled: Chiaki.settings.placeboDeinterlaceEnabled
                        visible: Chiaki.settings.placeboDeinterlaceEnabled
                        onToggled: Chiaki.settings.placeboDeinterlaceSkipSpatial = checked
                        Material.roundedScale: Material.SmallScale
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(no)")
                        visible: Chiaki.settings.placeboDeinterlaceEnabled
                    }
                }
            }

            Item {
                // Debanding
                GridLayout {
                    anchors {
                        top: parent.top
                        horizontalCenter: parent.horizontalCenter
                        topMargin: 20
                    }
                    columns: 3
                    rowSpacing: 10
                    columnSpacing: 20

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Deband Enabled:")
                    }

                    C.CheckBox {
                        text: qsTr("Enable Debanding")
                        checked: Chiaki.settings.placeboDebandEnabled
                        onToggled: Chiaki.settings.placeboDebandEnabled = checked
                    }


                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(Checked)")
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Deband Preset:")
                        visible: Chiaki.settings.placeboDebandEnabled
                    }

                    C.ComboBox {
                        Layout.preferredWidth: 400
                        model: [qsTr("Custom"), qsTr("Default")]
                        currentIndex: Chiaki.settings.placeboDebandPreset
                        onActivated: index => Chiaki.settings.placeboDebandPreset = index
                        visible: Chiaki.settings.placeboDebandEnabled
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(Custom)")
                        visible: Chiaki.settings.placeboDebandEnabled
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Deband Iterations:")
                        visible: Chiaki.settings.placeboDebandEnabled && (Chiaki.settings.placeboDebandPreset == 0)
                    }

                    C.Slider {
                        Layout.preferredWidth: 250
                        from: 0
                        to: 16
                        stepSize: 1
                        value: Chiaki.settings.placeboDebandIterations
                        onMoved: Chiaki.settings.placeboDebandIterations = value
                        visible: Chiaki.settings.placeboDebandEnabled && (Chiaki.settings.placeboDebandPreset == 0)

                        Label {
                            anchors {
                                left: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 10
                            }
                            text: qsTr("%1").arg(parent.value)
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(1)")
                        visible: Chiaki.settings.placeboDebandEnabled && (Chiaki.settings.placeboDebandPreset == 0)
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Deband Threshold:")
                        visible: Chiaki.settings.placeboDebandEnabled && (Chiaki.settings.placeboDebandPreset == 0)
                    }

                    C.Slider {
                        Layout.preferredWidth: 250
                        from: 0.0
                        to: 1000.0
                        stepSize: 0.1
                        value: Chiaki.settings.placeboDebandThreshold
                        onMoved: Chiaki.settings.placeboDebandThreshold = value
                        visible: Chiaki.settings.placeboDebandEnabled && (Chiaki.settings.placeboDebandPreset == 0)

                        Label {
                            anchors {
                                left: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 10
                            }
                            text: qsTr(parent.value.toFixed(1))
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(3.0)")
                        visible: Chiaki.settings.placeboDebandEnabled && (Chiaki.settings.placeboDebandPreset == 0)
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Deband Radius:")
                        visible: Chiaki.settings.placeboDebandEnabled && (Chiaki.settings.placeboDebandPreset == 0)
                    }

                    C.Slider {
                        Layout.preferredWidth: 250
                        from: 0.0
                        to: 1000.0
                        stepSize: 0.1
                        value: Chiaki.settings.placeboDebandRadius
                        onMoved: Chiaki.settings.placeboDebandRadius = value
                        visible: Chiaki.settings.placeboDebandEnabled && (Chiaki.settings.placeboDebandPreset == 0)

                        Label {
                            anchors {
                                left: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 10
                            }
                            text: qsTr(parent.value.toFixed(1))
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(16.0)")
                        visible: Chiaki.settings.placeboDebandEnabled && (Chiaki.settings.placeboDebandPreset == 0)
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Deband Grain:")
                        visible: Chiaki.settings.placeboDebandEnabled && (Chiaki.settings.placeboDebandPreset == 0)
                    }

                    C.Slider {
                        Layout.preferredWidth: 250
                        from: 0.0
                        to: 1000.0
                        stepSize: 0.1
                        value: Chiaki.settings.placeboDebandGrain
                        onMoved: Chiaki.settings.placeboDebandGrain = value
                        visible: Chiaki.settings.placeboDebandEnabled && (Chiaki.settings.placeboDebandPreset == 0)

                        Label {
                            anchors {
                                left: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 10
                            }
                            text: qsTr(parent.value.toFixed(1))
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(4.0)")
                        visible: Chiaki.settings.placeboDebandEnabled && (Chiaki.settings.placeboDebandPreset == 0)
                    }
                }
            }

            Item {
                // Sigmoidization
                GridLayout {
                    anchors {
                        top: parent.top
                        horizontalCenter: parent.horizontalCenter
                        topMargin: 20
                    }
                    columns: 3
                    rowSpacing: 10
                    columnSpacing: 20

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Sigmoidization Enabled:")
                    }

                    C.CheckBox {
                        text: qsTr("Enable Sigmoidization")
                        checked: Chiaki.settings.placeboSigmoidEnabled
                        onToggled: Chiaki.settings.placeboSigmoidEnabled = checked
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(Checked)")
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Sigmoid Preset:")
                        visible: Chiaki.settings.placeboSigmoidEnabled
                    }

                    C.ComboBox {
                        Layout.preferredWidth: 400
                        model: [qsTr("Custom"), qsTr("Default")]
                        currentIndex: Chiaki.settings.placeboSigmoidPreset
                        onActivated: index => Chiaki.settings.placeboSigmoidPreset = index
                        visible: Chiaki.settings.placeboSigmoidEnabled
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(Custom)")
                        visible: Chiaki.settings.placeboSigmoidEnabled
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Sigmoid Center:")
                        visible: Chiaki.settings.placeboSigmoidEnabled && (Chiaki.settings.placeboSigmoidPreset == 0)
                    }

                    C.Slider {
                        Layout.preferredWidth: 250
                        from: 0.00
                        to: 1.00
                        stepSize: 0.01
                        value: Chiaki.settings.placeboSigmoidCenter
                        onMoved: Chiaki.settings.placeboSigmoidCenter = value
                        visible: Chiaki.settings.placeboSigmoidEnabled && (Chiaki.settings.placeboSigmoidPreset == 0)

                        Label {
                            anchors {
                                left: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 10
                            }
                            text: qsTr(parent.value.toFixed(2))
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(0.75)")
                        visible: Chiaki.settings.placeboSigmoidEnabled && (Chiaki.settings.placeboSigmoidPreset == 0)
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Sigmoid Slope:")
                        visible: Chiaki.settings.placeboSigmoidEnabled && (Chiaki.settings.placeboSigmoidPreset == 0)
                    }

                    C.Slider {
                        Layout.preferredWidth: 250
                        from: 1.0
                        to: 20.0
                        stepSize: 0.1
                        value: Chiaki.settings.placeboSigmoidSlope
                        onMoved: Chiaki.settings.placeboSigmoidSlope = value
                        visible: Chiaki.settings.placeboSigmoidEnabled && (Chiaki.settings.placeboSigmoidPreset == 0)

                        Label {
                            anchors {
                                left: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 10
                            }
                            text: qsTr(parent.value.toFixed(1))
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(6.5)")
                        visible: Chiaki.settings.placeboSigmoidEnabled && (Chiaki.settings.placeboSigmoidPreset == 0)
                    }
                }
            }

            Item {
                // Color
                GridLayout {
                    anchors {
                        top: parent.top
                        horizontalCenter: parent.horizontalCenter
                        topMargin: 20
                    }
                    columns: 3
                    rowSpacing: 10
                    columnSpacing: 20

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Color Adjustment Enabled:")
                    }

                    C.CheckBox {
                        text: qsTr("Enable Color Adjustment")
                        checked: Chiaki.settings.placeboColorAdjustmentEnabled
                        onToggled: Chiaki.settings.placeboColorAdjustmentEnabled = checked
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(Checked)")
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Color Adjustment Preset:")
                        visible: Chiaki.settings.placeboColorAdjustmentEnabled
                    }

                    C.ComboBox {
                        Layout.preferredWidth: 400
                        model: [qsTr("Custom"), qsTr("Neutral")]
                        currentIndex: Chiaki.settings.placeboColorAdjustmentPreset
                        onActivated: index => Chiaki.settings.placeboColorAdjustmentPreset = index
                        visible: Chiaki.settings.placeboColorAdjustmentEnabled
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(Custom)")
                        visible: Chiaki.settings.placeboColorAdjustmentEnabled
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Brightness:")
                        visible: Chiaki.settings.placeboColorAdjustmentEnabled && (Chiaki.settings.placeboColorAdjustmentPreset == 0)
                    }

                    C.Slider {
                        Layout.preferredWidth: 250
                        from: -1.00
                        to: 1.00
                        stepSize: 0.01
                        value: Chiaki.settings.placeboColorAdjustmentBrightness
                        onMoved: Chiaki.settings.placeboColorAdjustmentBrightness = value
                        visible: Chiaki.settings.placeboColorAdjustmentEnabled && (Chiaki.settings.placeboColorAdjustmentPreset == 0)

                        Label {
                            anchors {
                                left: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 10
                            }
                            text: qsTr(parent.value.toFixed(2))
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(0.00)")
                        visible: Chiaki.settings.placeboColorAdjustmentEnabled && (Chiaki.settings.placeboColorAdjustmentPreset == 0)
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Contrast:")
                        visible: Chiaki.settings.placeboColorAdjustmentEnabled && (Chiaki.settings.placeboColorAdjustmentPreset == 0)
                    }

                    C.Slider {
                        Layout.preferredWidth: 250
                        from: 0.0
                        to: 100.0
                        stepSize: 0.1
                        value: Chiaki.settings.placeboColorAdjustmentContrast
                        onMoved: Chiaki.settings.placeboColorAdjustmentContrast = value
                        visible: Chiaki.settings.placeboColorAdjustmentEnabled && (Chiaki.settings.placeboColorAdjustmentPreset == 0)

                        Label {
                            anchors {
                                left: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 10
                            }
                            text: qsTr(parent.value.toFixed(1))
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(1.0)")
                        visible: Chiaki.settings.placeboColorAdjustmentEnabled && (Chiaki.settings.placeboColorAdjustmentPreset == 0)
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Saturation:")
                        visible: Chiaki.settings.placeboColorAdjustmentEnabled && (Chiaki.settings.placeboColorAdjustmentPreset == 0)
                    }

                    C.Slider {
                        Layout.preferredWidth: 250
                        from: 0.0
                        to: 100.0
                        stepSize: 0.1
                        value: Chiaki.settings.placeboColorAdjustmentSaturation
                        onMoved: Chiaki.settings.placeboColorAdjustmentSaturation = value
                        visible: Chiaki.settings.placeboColorAdjustmentEnabled && (Chiaki.settings.placeboColorAdjustmentPreset == 0)

                        Label {
                            anchors {
                                left: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 10
                            }
                            text: qsTr(parent.value.toFixed(1))
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(1.0)")
                        visible: Chiaki.settings.placeboColorAdjustmentEnabled && (Chiaki.settings.placeboColorAdjustmentPreset == 0)
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Hue:")
                        visible: Chiaki.settings.placeboColorAdjustmentEnabled && (Chiaki.settings.placeboColorAdjustmentPreset == 0)
                    }

                    C.Slider {
                        Layout.preferredWidth: 250
                        from: 0.00
                        to: 6.28
                        stepSize: 0.01
                        value: Chiaki.settings.placeboColorAdjustmentHue
                        onMoved: Chiaki.settings.placeboColorAdjustmentHue = value
                        visible: Chiaki.settings.placeboColorAdjustmentEnabled && (Chiaki.settings.placeboColorAdjustmentPreset == 0)

                        Label {
                            anchors {
                                left: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 10
                            }
                            text: qsTr(parent.value.toFixed(2))
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(0.00)")
                        visible: Chiaki.settings.placeboColorAdjustmentEnabled && (Chiaki.settings.placeboColorAdjustmentPreset == 0)
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Gamma:")
                        visible: Chiaki.settings.placeboColorAdjustmentEnabled && (Chiaki.settings.placeboColorAdjustmentPreset == 0)
                    }

                    C.Slider {
                        Layout.preferredWidth: 250
                        from: 0.0
                        to: 100.0
                        stepSize: 0.1
                        value: Chiaki.settings.placeboColorAdjustmentGamma
                        onMoved: Chiaki.settings.placeboColorAdjustmentGamma = value
                        visible: Chiaki.settings.placeboColorAdjustmentEnabled && (Chiaki.settings.placeboColorAdjustmentPreset == 0)

                        Label {
                            anchors {
                                left: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 10
                            }
                            text: qsTr(parent.value.toFixed(1))
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(1.0)")
                        visible: Chiaki.settings.placeboColorAdjustmentEnabled && (Chiaki.settings.placeboColorAdjustmentPreset == 0)
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Temperature:")
                        visible: Chiaki.settings.placeboColorAdjustmentEnabled && (Chiaki.settings.placeboColorAdjustmentPreset == 0)
                    }

                    C.Slider {
                        Layout.preferredWidth: 250
                        from: -1.143
                        to: 5.286
                        stepSize: 0.001
                        value: Chiaki.settings.placeboColorAdjustmentTemperature
                        onMoved: Chiaki.settings.placeboColorAdjustmentTemperature = value
                        visible: Chiaki.settings.placeboColorAdjustmentEnabled && (Chiaki.settings.placeboColorAdjustmentPreset == 0)

                        Label {
                            anchors {
                                left: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 10
                            }
                            text: qsTr(parent.value.toFixed(3))
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(0.000)")
                        visible: Chiaki.settings.placeboColorAdjustmentEnabled && (Chiaki.settings.placeboColorAdjustmentPreset == 0)
                    }
                }
            }

            Item {
                // Peak Detection
                GridLayout {
                    anchors {
                        top: parent.top
                        horizontalCenter: parent.horizontalCenter
                        topMargin: 20
                    }
                    columns: 3
                    rowSpacing: 10
                    columnSpacing: 20

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("HDR Peak Detection Enabled:")
                    }

                    C.CheckBox {
                        text: qsTr("Enable HDR Peak Detection")
                        checked: Chiaki.settings.placeboPeakDetectionEnabled
                        onToggled: Chiaki.settings.placeboPeakDetectionEnabled = checked
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(Checked)")
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Peak Detection Preset:")
                        visible: Chiaki.settings.placeboPeakDetectionEnabled
                    }

                    C.ComboBox {
                        Layout.preferredWidth: 400
                        model: [qsTr("Custom"), qsTr("Default"), qsTr("High Quality")]
                        currentIndex: Chiaki.settings.placeboPeakDetectionPreset
                        onActivated: index => Chiaki.settings.placeboPeakDetectionPreset = index
                        visible: Chiaki.settings.placeboPeakDetectionEnabled
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(High Quality)")
                        visible: Chiaki.settings.placeboPeakDetectionEnabled
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Peak Smoothing Period:")
                        visible: Chiaki.settings.placeboPeakDetectionEnabled && (Chiaki.settings.placeboPeakDetectionPreset == 0)
                    }

                    C.Slider {
                        Layout.preferredWidth: 250
                        from: 0.0
                        to: 1000.0
                        stepSize: 0.1
                        value: Chiaki.settings.placeboPeakDetectionPeakSmoothingPeriod
                        onMoved: Chiaki.settings.placeboPeakDetectionPeakSmoothingPeriod = value
                        visible: Chiaki.settings.placeboPeakDetectionEnabled && (Chiaki.settings.placeboPeakDetectionPreset == 0)

                        Label {
                            anchors {
                                left: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 10
                            }
                            text: qsTr(parent.value.toFixed(1))
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(20.0)")
                        visible: Chiaki.settings.placeboPeakDetectionEnabled && (Chiaki.settings.placeboPeakDetectionPreset == 0)
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Scene Threshold Low:")
                        visible: Chiaki.settings.placeboPeakDetectionEnabled && (Chiaki.settings.placeboPeakDetectionPreset == 0)
                    }

                    C.Slider {
                        Layout.preferredWidth: 250
                        from: 0.0
                        to: 100.0
                        stepSize: 0.1
                        value: Chiaki.settings.placeboPeakDetectionSceneThresholdLow
                        onMoved: Chiaki.settings.placeboPeakDetectionSceneThresholdLow = value
                        visible: Chiaki.settings.placeboPeakDetectionEnabled && (Chiaki.settings.placeboPeakDetectionPreset == 0)

                        Label {
                            anchors {
                                left: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 10
                            }
                            text: qsTr(parent.value.toFixed(1))
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(1.0)")
                        visible: Chiaki.settings.placeboPeakDetectionEnabled && (Chiaki.settings.placeboPeakDetectionPreset == 0)
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Scene Threshold High:")
                        visible: Chiaki.settings.placeboPeakDetectionEnabled && (Chiaki.settings.placeboPeakDetectionPreset == 0)
                    }

                    C.Slider {
                        Layout.preferredWidth: 250
                        from: 0.0
                        to: 100.0
                        stepSize: 0.1
                        value: Chiaki.settings.placeboPeakDetectionSceneThresholdHigh
                        onMoved: Chiaki.settings.placeboPeakDetectionSceneThresholdHigh = value
                        visible: Chiaki.settings.placeboPeakDetectionEnabled && (Chiaki.settings.placeboPeakDetectionPreset == 0)

                        Label {
                            anchors {
                                left: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 10
                            }
                            text: qsTr(parent.value.toFixed(1))
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(3.0)")
                        visible: Chiaki.settings.placeboPeakDetectionEnabled && (Chiaki.settings.placeboPeakDetectionPreset == 0)
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Peak Percentile:")
                        visible: Chiaki.settings.placeboPeakDetectionEnabled && (Chiaki.settings.placeboPeakDetectionPreset == 0)
                    }

                    C.Slider {
                        Layout.preferredWidth: 250
                        from: 0.0
                        to: 100.0
                        stepSize: 0.001
                        value: Chiaki.settings.placeboPeakDetectionPeakPercentile
                        onMoved: Chiaki.settings.placeboPeakDetectionPeakPercentile = value
                        visible: Chiaki.settings.placeboPeakDetectionEnabled && (Chiaki.settings.placeboPeakDetectionPreset == 0)

                        Label {
                            anchors {
                                left: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 10
                            }
                            text: qsTr(parent.value.toFixed(3))
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(99.995)")
                        visible: Chiaki.settings.placeboPeakDetectionEnabled && (Chiaki.settings.placeboPeakDetectionPreset == 0)
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Black Cutoff:")
                        visible: Chiaki.settings.placeboPeakDetectionEnabled && (Chiaki.settings.placeboPeakDetectionPreset == 0)
                    }

                    C.Slider {
                        Layout.preferredWidth: 250
                        from: 0.0
                        to: 100.0
                        stepSize: 0.1
                        value: Chiaki.settings.placeboPeakDetectionBlackCutoff
                        onMoved: Chiaki.settings.placeboPeakDetectionBlackCutoff = value
                        visible: Chiaki.settings.placeboPeakDetectionEnabled && (Chiaki.settings.placeboPeakDetectionPreset == 0)

                        Label {
                            anchors {
                                left: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 10
                            }
                            text: qsTr(parent.value.toFixed(1))
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(1.0)")
                        visible: Chiaki.settings.placeboPeakDetectionEnabled && (Chiaki.settings.placeboPeakDetectionPreset == 0)
                    }

                    Label {
                    Layout.alignment: Qt.AlignRight
                    text: qsTr("Allow Delayed Peak:")
                    visible: Chiaki.settings.placeboPeakDetectionEnabled && (Chiaki.settings.placeboPeakDetectionPreset == 0)
                    }   

                    C.CheckBox {
                        text: qsTr("Enable 1-Frame Delayed Peak")
                        checked: Chiaki.settings.placeboPeakDetectionAllowDelayedPeak
                        onToggled: Chiaki.settings.placeboPeakDetectionAllowDelayedPeak = checked
                        visible: Chiaki.settings.placeboPeakDetectionEnabled && (Chiaki.settings.placeboPeakDetectionPreset == 0)
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(Unchecked)")
                        visible: Chiaki.settings.placeboPeakDetectionEnabled && (Chiaki.settings.placeboPeakDetectionPreset == 0)
                    }
                }
            }

            Item {
                // Color Mapping
                GridLayout {
                    anchors {
                        top: parent.top
                        horizontalCenter: parent.horizontalCenter
                        topMargin: 20
                    }
                    columns: 3
                    rowSpacing: 10
                    columnSpacing: 20

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Color Mapping Enabled:")
                    }

                    C.CheckBox {
                        text: qsTr("Use Color Mapping Settings")
                        checked: Chiaki.settings.placeboColorMappingEnabled
                        onToggled: Chiaki.settings.placeboColorMappingEnabled = checked
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(Checked)")
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Color Mapping Preset:")
                        visible: Chiaki.settings.placeboColorMappingEnabled
                    }

                    C.ComboBox {
                        Layout.preferredWidth: 400
                        model: [qsTr("Custom"), qsTr("Default"), qsTr("High Quality")]
                        currentIndex: Chiaki.settings.placeboColorMappingPreset
                        onActivated: index => Chiaki.settings.placeboColorMappingPreset = index
                        visible: Chiaki.settings.placeboColorMappingEnabled
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(High Quality)")
                        visible: Chiaki.settings.placeboColorMappingEnabled
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Color Mapping Settings:")
                        visible: Chiaki.settings.placeboColorMappingEnabled && Chiaki.settings.placeboColorMappingPreset == 0
                    }

                    C.Button {
                        text: qsTr("Open")
                        onClicked: root.showPlaceboColorMappingDialog()
                        visible: Chiaki.settings.placeboColorMappingEnabled && Chiaki.settings.placeboColorMappingPreset == 0
                        Material.roundedScale: Material.SmallScale
                    }
                }
            }
        }
    }
}
