import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    anchors.fill: parent

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.sp5
        spacing: Theme.sp4

        Text { text: "Inference Engine"; color: Theme.text; font.pixelSize: Theme.fontSizeXl; font.bold: true }
        Text { text: "llama.cpp is required for local inference. Install or update it here from GitHub releases."; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm; wrapMode: Text.Wrap; Layout.fillWidth: true }

        GroupBox {
            title: "Status"
            Layout.fillWidth: true
            ColumnLayout {
                spacing: Theme.sp2
                Text { text: "Installed: " + (engineInstaller.getIsInstalled() ? "Yes" : "No"); color: Theme.text }
                Text { text: "Version: " + (engineInstaller.getInstalledVersion() || "—"); color: Theme.textMuted }
                Text { text: "Path: " + engineInstaller.getEnginePath(); color: Theme.textMuted; font.pixelSize: Theme.fontSizeXs; wrapMode: Text.Wrap; Layout.fillWidth: true }
            }
        }

        GroupBox {
            title: "Install / Update"
            Layout.fillWidth: true
            ColumnLayout {
                spacing: Theme.sp3
                Button {
                    text: "Check for latest release"
                    onClicked: engineInstaller.checkLatest()
                }
                ProgressBar {
                    id: bar
                    Layout.fillWidth: true
                    from: 0; to: 100; value: 0
                }
                Text { id: statusText; text: "Idle"; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm }
                RowLayout {
                    spacing: Theme.sp2
                    Button {
                        text: "Install"
                        enabled: !engineInstaller.getIsInstalled()
                        onClicked: engineInstaller.install()
                    }
                    Button {
                        text: "Uninstall"
                        enabled: engineInstaller.getIsInstalled()
                        onClicked: engineInstaller.uninstall()
                    }
                }
            }
        }

        Connections {
            target: engineInstaller
            function onProgress(received, total) {
                if (total > 0) bar.value = received / total * 100;
                statusText.text = "Downloading... " + Math.round(received / 1024) + " KB";
            }
            function onLatestChecked(version, assetUrl, assetName) {
                statusText.text = "Latest: " + version + " (" + assetName + ")";
            }
            function onLatestCheckFailed(error) {
                statusText.text = "Check failed: " + error;
            }
            function onFinished(version, path) {
                statusText.text = "Installed " + version;
                bar.value = 100;
            }
            function onFailed(error) {
                statusText.text = "Failed: " + error;
            }
        }
    }
}
