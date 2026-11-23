import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property bool compact: false
    property string currentView: "home"
    signal viewSelected(string view)

    Rectangle {
        anchors.fill: parent
        color: Theme.panel
        border.color: Theme.border
        border.width: 1

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Theme.sp3
            spacing: Theme.sp2

            // Logo
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.sp2
                Rectangle {
                    width: 32; height: 32; radius: Theme.radiusMd
                    color: Theme.accent
                    Text { anchors.centerIn: parent; text: "M"; color: "white"; font.bold: true; font.pixelSize: 16 }
                }
                Text {
                    visible: !root.compact
                    text: "ModelBench"
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeLg
                    font.bold: true
                    font.family: Theme.fontSans
                }
            }

            Item { Layout.preferredHeight: Theme.sp2 }

            Repeater {
                model: [
                    { id: "home", label: "Home", icon: "H" },
                    { id: "chat", label: "Chat", icon: "C" },
                    { id: "models", label: "Models", icon: "Mo" },
                    { id: "engine", label: "Engine", icon: "En" },
                    { id: "mcp", label: "MCP Hub", icon: "Mc" },
                    { id: "skills", label: "Skills Hub", icon: "Sk" },
                    { id: "settings", label: "Settings", icon: "Se" },
                    { id: "account", label: "Account", icon: "Ac" }
                ]
                delegate: Rectangle {
                    Layout.fillWidth: true
                    height: 40
                    radius: Theme.radiusMd
                    color: root.currentView === modelData.id ? Theme.panelAlt : "transparent"
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.sp3
                        anchors.rightMargin: Theme.sp3
                        spacing: Theme.sp3
                        Text {
                            text: modelData.icon
                            color: root.currentView === modelData.id ? Theme.accent : Theme.textMuted
                            font.pixelSize: Theme.fontSizeSm
                            font.bold: true
                            font.family: Theme.fontMono
                        }
                        Text {
                            visible: !root.compact
                            text: modelData.label
                            color: root.currentView === modelData.id ? Theme.text : Theme.textMuted
                            font.pixelSize: Theme.fontSizeMd
                            font.family: Theme.fontSans
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.viewSelected(modelData.id)
                    }
                }
            }

            Item { Layout.fillHeight: true }

            // Bottom: subscription shortcut
            Rectangle {
                Layout.fillWidth: true
                height: 44
                radius: Theme.radiusMd
                color: Theme.panelAlt
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.sp3
                    anchors.rightMargin: Theme.sp3
                    spacing: Theme.sp2
                    Text { text: "Free"; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm }
                    Item { Layout.fillWidth: true }
                    Text { visible: !root.compact; text: "Upgrade"; color: Theme.accent; font.pixelSize: Theme.fontSizeSm; font.bold: true }
                }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.viewSelected("subscription") }
            }
        }
    }
}
