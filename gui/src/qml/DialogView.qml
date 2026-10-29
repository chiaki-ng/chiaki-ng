import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

import "controls" as C

Item {
    id: dialog
    property alias header: headerLabel.text
    property alias title: titleLabel.text
    property alias buttonText: okButton.text
    property alias buttonEnabled: okButton.enabled
    property alias buttonVisible: okButton.visible
    property Item restoreFocusItem
    default property Item mainItem: null

    signal accepted()
    signal rejected()

    function close() {
        root.closeDialog();
    }

    Keys.onEscapePressed: close()

    Keys.onMenuPressed: {
        if (okButton.enabled)
            okButton.clicked()
    }

    StackView.onDeactivating: {
        restoreFocusItem = Window.window.activeFocusItem;
    }

    StackView.onActivated: {
        if (!restoreFocusItem) {
            let item = mainItem.nextItemInFocusChain();
            if (item)
                item.forceActiveFocus(Qt.TabFocusReason);
        } else {
            restoreFocusItem.forceActiveFocus(Qt.TabFocusReason);
            restoreFocusItem = null;
        }
    }

    onMainItemChanged: {
        if (mainItem) {
            mainItem.parent = contentItem;
            mainItem.anchors.fill = contentItem;
        }
    }

    Item {
        id: toolBar
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
        }
        height: 84

        RowLayout {
            anchors {
                fill: parent
                leftMargin: 24
                rightMargin: 24
            }
            spacing: 18

            C.IconButton {
                Layout.alignment: Qt.AlignVCenter
                iconSource: C.Theme.icon("back")
                size: 48
                focusPolicy: Qt.NoFocus
                onClicked: {
                    dialog.rejected();
                    dialog.close();
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: 2

                Text {
                    id: titleLabel
                    Layout.fillWidth: true
                    font.family: C.Theme.fontFamily
                    font.pixelSize: C.Theme.fontTitle
                    font.weight: Font.Bold
                    font.letterSpacing: -0.3
                    color: C.Theme.text
                    elide: Text.ElideRight
                }

                Text {
                    id: headerLabel
                    Layout.fillWidth: true
                    visible: text
                    font.family: C.Theme.fontFamily
                    font.pixelSize: C.Theme.fontCaption
                    color: C.Theme.textTertiary
                    elide: Text.ElideRight
                }
            }

            C.PillButton {
                id: okButton
                Layout.alignment: Qt.AlignVCenter
                kind: "primary"
                focusPolicy: Qt.NoFocus
                iconSource: "qrc:/icons/options.svg"
                onClicked: dialog.accepted()
            }
        }

        Rectangle {
            anchors {
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }
            height: 1
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 0.2; color: C.Theme.strokeStrong }
                GradientStop { position: 0.8; color: C.Theme.strokeStrong }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }
    }

    Item {
        id: contentItem
        anchors {
            top: toolBar.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
    }
}
