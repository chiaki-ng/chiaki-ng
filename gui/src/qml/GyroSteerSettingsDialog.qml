import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

import org.streetpea.chiaking

import "controls" as C

DialogView {
    id: dialog
    title: qsTr("Gyro Steering")
    buttonVisible: false

    Item {
        anchors.fill: parent

        Timer {
            id: gyroPreviewTimer
            interval: 250
            repeat: true
            running: gyroFlick.visible
            onTriggered: {
                gyroAngleValue.text = Chiaki.settings.gyroSteerAngle().toFixed(1) + "°";
                gyroLeftXValue.text = Chiaki.settings.gyroSteerLeftX().toFixed(2);
            }
        }

        Flickable {
            id: gyroFlick
            anchors {
                fill: parent
                topMargin: 20
                bottomMargin: 20
            }
            clip: true
            contentWidth: Math.max(width, gyroLayout.width)
            contentHeight: gyroLayout.height
            flickableDirection: Flickable.AutoFlickIfNeeded
            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AlwaysOn
                visible: gyroFlick.contentHeight > gyroFlick.height
            }
            ColumnLayout {
                id: gyroLayout
                anchors {
                    top: parent.top
                    horizontalCenter: parent.horizontalCenter
                }
                spacing: 10

                GridLayout {
                    columns: 3
                    rowSpacing: 10
                    columnSpacing: 20

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Recognized Steering Angle:")
                    }
                    Label {
                        id: gyroAngleValue
                        Layout.preferredWidth: 150
                        text: "--"
                    }
                    Item {
                        Layout.preferredWidth: 100
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Output Left Stick X:")
                    }
                    Label {
                        id: gyroLeftXValue
                        Layout.preferredWidth: 150
                        text: "--"
                    }
                    Item {
                        Layout.preferredWidth: 100
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Sensitivity (Full Deflection):")
                    }
                    C.Slider {
                        Layout.preferredWidth: 250
                        from: 10
                        to: 90
                        stepSize: 5
                        value: Chiaki.settings.gyroSteeringSensitivity
                        onMoved: Chiaki.settings.gyroSteeringSensitivity = value
                        Label {
                            anchors {
                                left: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 10
                            }
                            text: Math.round(parent.value) + "°"
                        }
                    }
                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(30°)")
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Deadzone:")
                    }
                    C.Slider {
                        Layout.preferredWidth: 250
                        from: 0
                        to: 10
                        stepSize: 0.5
                        value: Chiaki.settings.gyroSteeringDeadzone
                        onMoved: Chiaki.settings.gyroSteeringDeadzone = value
                        Label {
                            anchors {
                                left: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 10
                            }
                            text: parent.value.toFixed(1) + qsTr("°")
                        }
                    }
                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(3°)")
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Invert Steering:")
                    }
                    C.CheckBox {
                        text: qsTr("Invert steering direction")
                        checked: Chiaki.settings.gyroSteeringInvert
                        onToggled: Chiaki.settings.gyroSteeringInvert = checked
                    }
                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(Unchecked)")
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Rest Point:")
                    }
                    C.Button {
                        text: qsTr("Set Rest Point")
                        onClicked: Chiaki.settings.setGyroSteerRestPoint()
                    }
                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("(Recenter)")
                    }
                }
            }
        }
    }
}
