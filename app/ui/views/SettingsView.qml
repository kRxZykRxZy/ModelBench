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
            anchors.margins: Theme.sp5
            spacing: Theme.sp5

            Text { text: "Settings"; color: Theme.text; font.pixelSize: Theme.fontSizeXl; font.bold: true }

            // Appearance
            GroupBox {
                title: "Appearance"
                Layout.fillWidth: true
                RowLayout {
                    spacing: Theme.sp3
                    Text { text: "Theme"; color: Theme.text }
                    ComboBox { model: ["System", "Dark", "Light"] }
                }
            }

            // Hugging Face token
            GroupBox {
                title: "Hugging Face Access Token (optional)"
                Layout.fillWidth: true
                ColumnLayout {
                    spacing: Theme.sp2
                    Text { text: "Optional. Access private repos and gated models you've been granted."; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    RowLayout {
                        Layout.fillWidth: true
                        TextField { Layout.fillWidth: true; placeholderText: "hf_..."; echoMode: TextInput.Password }
                        Button { text: "Verify" }
                        Button { text: "Remove" }
                    }
                }
            }

            // Hardware
            GroupBox {
                title: "Hardware"
                Layout.fillWidth: true
                Text { text: "Hardware detection runs on first launch."; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm }
            }

            // Storage
            GroupBox {
                title: "Model Storage Directory"
                Layout.fillWidth: true
                RowLayout {
                    Layout.fillWidth: true
                    TextField { Layout.fillWidth: true; text: "" ; placeholderText: "Default" }
                    Button { text: "Browse" }
                }
            }

            // Local API
            GroupBox {
                title: "Local API Server"
                Layout.fillWidth: true
                ColumnLayout {
                    spacing: Theme.sp2
                    RowLayout {
                        Text { text: "Bind address"; color: Theme.text }
                        TextField { text: "127.0.0.1" }
                        Text { text: "Port"; color: Theme.text }
                        TextField { text: "8080"; Layout.preferredWidth: 80 }
                    }
                    Button { text: "Start server" }
                }
            }
        }
    }
}
