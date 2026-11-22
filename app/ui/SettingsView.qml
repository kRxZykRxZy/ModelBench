import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property var hw: app.hardwareInfo()

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        ColumnLayout {
            width: parent.width
            anchors.margins: Theme.sp5
            spacing: Theme.sp4

            Text { text: "Settings"; color: Theme.text; font.pixelSize: Theme.fontSizeXl; font.bold: true }

            GroupBox {
                title: "Appearance"
                Layout.fillWidth: true
                RowLayout {
                    anchors.fill: parent
                    Text { text: "Theme"; color: Theme.text }
                    ComboBox {
                        model: ["Dark (default)"]
                        currentIndex: 0
                        enabled: false
                    }
                    Text { text: "Light mode coming soon"; color: Theme.textSubtle; font.pixelSize: Theme.fontSizeXs }
                }
            }

            GroupBox {
                title: "Hugging Face Access Token (optional)"
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    spacing: Theme.sp2
                    Text { text: "Optional. Access private repos and gated models you've been granted. Stored securely in OS credential storage."; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    RowLayout {
                        Layout.fillWidth: true
                        TextField { id: hfToken; Layout.fillWidth: true; placeholderText: "hf_..."; echoMode: TextInput.Password; color: Theme.text }
                        Button { text: "Save"; onClicked: { hfClient.setToken(hfToken.text); statusText.text = hfToken.text ? "Token set for this session" : "Token cleared" } }
                    }
                }
            }

            GroupBox {
                title: "Hardware"
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    spacing: Theme.sp2
                    Text { text: "CPU: " + (root.hw.cpuModel || "Unknown"); color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    Text { text: "Cores: " + (root.hw.cpuCores || 0) + " / Threads: " + (root.hw.cpuThreads || 0); color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm }
                    Text { text: "RAM: " + Math.round((root.hw.ramBytes || 0)/1073741824) + " GB"; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm }
                    Text { text: "Backends: " + ((root.hw.supportedBackends || []).join(", ")); color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm }
                }
            }

            GroupBox {
                title: "Inference Engine"
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    spacing: Theme.sp2
                    Text { text: engineInstaller.getIsInstalled() ? ("Installed: " + engineInstaller.getInstalledVersion()) : "Not installed"; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm }
                    Text { text: engineInstaller.getEnginePath(); color: Theme.textSubtle; font.pixelSize: Theme.fontSizeXs; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    Button { text: "Open Engine Installer"; onClicked: window.currentView = "engine" }
                }
            }

            Text { id: statusText; text: ""; color: Theme.success; font.pixelSize: Theme.fontSizeSm }

            Text { text: "ModelFit v0.1.0 · Local data: " + app.dataDirectory; color: Theme.textSubtle; font.pixelSize: Theme.fontSizeXs; wrapMode: Text.Wrap; Layout.fillWidth: true }
        }
    }
}
