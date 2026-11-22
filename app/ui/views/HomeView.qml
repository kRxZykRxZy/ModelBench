import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    anchors.fill: parent

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            anchors.margins: Theme.sp6
            spacing: Theme.sp5

            Text {
                text: "Welcome to ModelFit"
                color: Theme.text
                font.pixelSize: Theme.fontSize2xl
                font.bold: true
                font.family: Theme.fontSans
            }
            Text {
                text: "A native local AI workstation. Inference runs on your machine."
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeMd
                font.family: Theme.fontSans
            }

            // Hardware card
            Rectangle {
                Layout.fillWidth: true
                height: 160
                radius: Theme.radiusLg
                color: Theme.panel
                border.color: Theme.border
                border.width: 1
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.sp4
                    spacing: Theme.sp2
                    Text { text: "Hardware"; color: Theme.text; font.bold: true; font.pixelSize: Theme.fontSizeLg }
                    Text { text: "Detection runs on first launch. Open Settings to view details."; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm }
                }
            }

            // Quick actions
            RowLayout {
                spacing: Theme.sp3
                Repeater {
                    model: ["Chat", "Models", "MCP Hub", "Skills Hub"]
                    delegate: Rectangle {
                        width: 140; height: 80; radius: Theme.radiusLg
                        color: Theme.panel; border.color: Theme.border; border.width: 1
                        Text { anchors.centerIn: parent; text: modelData; color: Theme.text; font.pixelSize: Theme.fontSizeMd; font.bold: true }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor }
                    }
                }
            }
        }
    }
}
