import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    function refresh() { serverList.model = mcpServers.getServers() }

    Component.onCompleted: refresh()
    Connections { target: mcpServers; function onConfigsChanged() { root.refresh() } }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.sp5
        spacing: Theme.sp4

        Text { text: "MCP Hub"; color: Theme.text; font.pixelSize: Theme.fontSizeXl; font.bold: true }
        Text { text: "Add Model Context Protocol servers to give your model tools."; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm; wrapMode: Text.Wrap; Layout.fillWidth: true }

        GroupBox {
            title: "Add MCP Server"
            Layout.fillWidth: true
            GridLayout {
                width: parent.width
                columns: 2
                columnSpacing: Theme.sp3
                rowSpacing: Theme.sp2
                Text { text: "Name"; color: Theme.textMuted }
                TextField { id: nameField; placeholderText: "My server"; color: Theme.text }
                Text { text: "Transport"; color: Theme.textMuted }
                ComboBox { id: transportCombo; model: ["stdio", "streamable-http"] }
                Text { text: "Command (stdio)"; color: Theme.textMuted }
                TextField { id: cmdField; placeholderText: "npx -y @modelcontextprotocol/server-filesystem"; color: Theme.text }
                Text { text: "URL (http)"; color: Theme.textMuted }
                TextField { id: urlField; placeholderText: "https://..."; color: Theme.text }
                Item { Layout.columnSpan: 2; Layout.preferredHeight: Theme.sp1 }
                Button {
                    Layout.columnSpan: 2
                    text: "Add Server"
                    onClicked: {
                        if (!nameField.text.trim()) return
                        mcpServers.addServerConfig(nameField.text.trim(), transportCombo.currentText,
                                                   cmdField.text.trim(), "", urlField.text.trim())
                        nameField.text = ""; cmdField.text = ""; urlField.text = ""
                        statusText.text = "Server added"
                    }
                }
            }
        }

        Text { id: statusText; text: ""; color: Theme.success; font.pixelSize: Theme.fontSizeSm }

        Text { text: "Configured Servers"; color: Theme.text; font.bold: true; font.pixelSize: Theme.fontSizeLg }

        ListView {
            id: serverList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: []
            delegate: Rectangle {
                width: serverList.width
                implicitHeight: 60
                color: "transparent"
                border.color: Theme.border
                border.width: 1
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.sp3
                    ColumnLayout {
                        Layout.fillWidth: true
                        Text { text: modelData.name; color: Theme.text; font.bold: true; font.pixelSize: Theme.fontSizeMd }
                        Text { text: modelData.transport + "  ·  " + (modelData.command || modelData.url || ""); color: Theme.textMuted; font.pixelSize: Theme.fontSizeXs; elide: Text.ElideRight }
                    }
                    Button {
                        text: "Connect"
                        onClicked: { mcpServers.connectServerQml(modelData.id); statusText.text = "Connecting to " + modelData.name }
                    }
                    Button {
                        text: "Remove"
                        onClicked: { mcpServers.removeServer(modelData.id); statusText.text = "Removed" }
                    }
                }
            }
        }
    }
}
