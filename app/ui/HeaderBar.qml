import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    property string currentView: "home"
    color: Theme.bgElevated
    border.color: Theme.border
    border.width: 1

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.sp4
        anchors.rightMargin: Theme.sp4
        spacing: Theme.sp3

        Text {
            text: {
                switch (root.currentView) {
                    case "home": return "Home";
                    case "chat": return "Chat";
                    case "models": return "Models";
                    case "engine": return "Engine";
                    case "calculator": return "Model Fit Calculator";
                    case "mcp": return "MCP Hub";
                    case "skills": return "Skills Hub";
                    case "settings": return "Settings";
                    case "subscription": return "Subscription";
                }
                return "";
            }
            color: Theme.text
            font.pixelSize: Theme.fontSizeLg
            font.bold: true
            font.family: Theme.fontSans
        }

        Item { Layout.fillWidth: true }

        // Theme toggle
        Rectangle {
            width: 36; height: 36; radius: Theme.radiusMd
            color: Theme.panelAlt
            Text { anchors.centerIn: parent; text: Theme.dark ? "L" : "D"; color: Theme.text; font.pixelSize: Theme.fontSizeMd }
            MouseArea {
                anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                onClicked: Theme.mode = Theme.dark ? "light" : "dark"
            }
        }
    }
}
