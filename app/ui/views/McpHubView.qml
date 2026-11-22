import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    anchors.fill: parent

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.sp5
        spacing: Theme.sp4

        Text { text: "MCP Hub"; color: Theme.text; font.pixelSize: Theme.fontSizeXl; font.bold: true }
        Text { text: "Discover and install MCP servers from the official registry."; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.sp3
            TextField { Layout.fillWidth: true; placeholderText: "Search MCP servers..." }
            Button { text: "Search" }
        }

        TabBar {
            Layout.fillWidth: true
            TabButton { text: "Discover" }
            TabButton { text: "My Servers" }
            TabButton { text: "Installed Tools" }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Column {
                anchors.centerIn: parent
                spacing: Theme.sp2
                Text { text: "No MCP servers configured"; color: Theme.text; font.pixelSize: Theme.fontSizeLg; font.bold: true; anchors.horizontalCenter: parent.horizontalCenter }
                Text { text: "Add a stdio or remote MCP server to connect tools to your model."; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm; anchors.horizontalCenter: parent.horizontalCenter }
            }
        }
    }
}
