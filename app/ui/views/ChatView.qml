import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    anchors.fill: parent

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Conversation sidebar
        Rectangle {
            Layout.preferredWidth: 260
            Layout.fillHeight: true
            color: Theme.panel
            border.color: Theme.border
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.sp3
                spacing: Theme.sp2

                Button {
                    Layout.fillWidth: true
                    text: "+ New chat"
                    onClicked: { /* create conversation */ }
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    ListView {
                        model: 5
                        delegate: Rectangle {
                            width: parent.width; height: 36; radius: Theme.radiusSm
                            color: "transparent"
                            Text { anchors.verticalCenter: parent.verticalCenter; anchors.left: parent.left; anchors.leftMargin: Theme.sp2; text: "Conversation " + (index+1); color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm }
                        }
                    }
                }
            }
        }

        // Main chat area
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Messages
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                ListView {
                    id: msgList
                    model: 0
                    delegate: Item { }
                    // placeholder empty state
                }
                // Empty state
                Column {
                    anchors.centerIn: parent
                    spacing: Theme.sp2
                    Text { text: "Start a conversation"; color: Theme.text; font.pixelSize: Theme.fontSizeLg; font.bold: true; anchors.horizontalCenter: parent.horizontalCenter }
                    Text { text: "Select a model and send a message."; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm; anchors.horizontalCenter: parent.horizontalCenter }
                }
            }

            // Composer
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 120
                color: Theme.bgElevated
                border.color: Theme.border
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.sp3
                    spacing: Theme.sp2

                    TextArea {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        placeholderText: "Message ModelFit..."
                        wrapMode: TextArea.Wrap
                        background: Rectangle { color: "transparent" }
                    }
                    Button {
                        text: "Send"
                        Layout.preferredWidth: 80
                    }
                }
            }
        }
    }
}
