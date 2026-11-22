import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    anchors.fill: parent

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.sp5
        spacing: Theme.sp4

        Text { text: "Skills Hub"; color: Theme.text; font.pixelSize: Theme.fontSizeXl; font.bold: true }
        Text { text: "Browse and install reusable agent skills from skills.sh."; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.sp3
            TextField { Layout.fillWidth: true; placeholderText: "Search skills..." }
            Button { text: "Search" }
        }

        TabBar {
            Layout.fillWidth: true
            TabButton { text: "Discover" }
            TabButton { text: "Installed" }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Column {
                anchors.centerIn: parent
                spacing: Theme.sp2
                Text { text: "No skills installed"; color: Theme.text; font.pixelSize: Theme.fontSizeLg; font.bold: true; anchors.horizontalCenter: parent.horizontalCenter }
                Text { text: "Install a skill to extend your model's capabilities."; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm; anchors.horizontalCenter: parent.horizontalCenter }
            }
        }
    }
}
