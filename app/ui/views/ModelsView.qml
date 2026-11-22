import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    anchors.fill: parent

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.sp5
        spacing: Theme.sp4

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.sp3
            Text { text: "Model Library"; color: Theme.text; font.pixelSize: Theme.fontSizeXl; font.bold: true }
            Item { Layout.fillWidth: true }
            TextField { Layout.preferredWidth: 280; placeholderText: "Search Hugging Face..." }
            Button { text: "Search" }
        }

        // Featured / installed tabs
        TabBar {
            Layout.fillWidth: true
            TabButton { text: "Installed" }
            TabButton { text: "Discover" }
            TabButton { text: "Featured" }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            ListView {
                model: 0
                delegate: Item {}
                // empty state
            }
            Column {
                anchors.centerIn: parent
                spacing: Theme.sp2
                Text { text: "No models installed"; color: Theme.text; font.pixelSize: Theme.fontSizeLg; font.bold: true; anchors.horizontalCenter: parent.horizontalCenter }
                Text { text: "Discover and download a GGUF model to get started."; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm; anchors.horizontalCenter: parent.horizontalCenter }
            }
        }
    }
}
