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

            Text { text: "Welcome to ModelFit"; color: Theme.text; font.pixelSize: Theme.fontSize2xl; font.bold: true }
            Text { text: "Your local AI workstation. Inference runs on your machine — no account required."; color: Theme.textMuted; font.pixelSize: Theme.fontSizeMd; wrapMode: Text.Wrap; Layout.fillWidth: true }

            // Status card
            Rectangle {
                Layout.fillWidth: true
                radius: Theme.radiusLg
                color: Theme.panel
                border.color: Theme.border
                border.width: 1
                implicitHeight: 150
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.sp4
                    spacing: Theme.sp2
                    Text { text: "System Status"; color: Theme.text; font.bold: true; font.pixelSize: Theme.fontSizeLg }
                    RowLayout {
                        spacing: Theme.sp3
                        Rectangle { width: 10; height: 10; radius: 5; color: inference.isLoaded() ? Theme.success : Theme.warning }
                        Text { text: inference.isLoaded() ? "Model loaded and ready" : "No model loaded"; color: Theme.text; font.pixelSize: Theme.fontSizeSm }
                    }
                    Text { text: "Engine: " + (engineInstaller.getIsInstalled() ? "Installed " + engineInstaller.getInstalledVersion() : "Not installed (download in Engine view)"); color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm }
                    Text { text: "Conversations: " + conversations.getConversations().length; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm }
                }
            }

            // Hardware card
            Rectangle {
                Layout.fillWidth: true
                radius: Theme.radiusLg
                color: Theme.panel
                border.color: Theme.border
                border.width: 1
                implicitHeight: 170
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.sp4
                    spacing: Theme.sp2
                    Text { text: "Detected Hardware"; color: Theme.text; font.bold: true; font.pixelSize: Theme.fontSizeLg }
                    Text { text: "CPU: " + (root.hw.cpuModel || "Unknown"); color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    Text { text: "Cores: " + (root.hw.cpuCores || 0) + "  ·  RAM: " + Math.round((root.hw.ramBytes || 0)/1073741824) + " GB"; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm }
                    Text { text: "GPU: " + (root.hw.gpuModel || "Unknown") + " (" + (root.hw.gpuVendor || "Unknown") + ")"; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    Text { text: "Architecture: " + (root.hw.architecture || ""); color: Theme.textSubtle; font.pixelSize: Theme.fontSizeXs }
                }
            }

            // Quick actions
            Text { text: "Quick Actions"; color: Theme.text; font.bold: true; font.pixelSize: Theme.fontSizeLg }
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: Theme.sp3
                rowSpacing: Theme.sp3
                Repeater {
                    model: [
                        { label: "New Chat", view: "chat" },
                        { label: "Models", view: "models" },
                        { label: "Model Fit Calculator", view: "calculator" },
                        { label: "Engine", view: "engine" },
                        { label: "MCP Hub", view: "mcp" },
                        { label: "Skills Hub", view: "skills" }
                    ]
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 64
                        radius: Theme.radiusMd
                        color: Theme.panel
                        border.color: Theme.border
                        border.width: 1
                        Text { anchors.centerIn: parent; text: modelData.label; color: Theme.text; font.pixelSize: Theme.fontSizeMd; font.bold: true }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: window.currentView = modelData.view }
                    }
                }
            }
        }
    }
}
