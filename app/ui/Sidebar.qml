import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property bool compact: false
    property string currentView: "home"
    signal viewSelected(string view)

    // Credits balance (wired to the `credits` context property once exposed by the backend).
    property int creditsBalance: (credits && credits.balance !== undefined) ? credits.balance : 0
    property bool creditsLow: root.creditsBalance > 0 && root.creditsBalance < 10

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
                    { id: "account", label: "Account", icon: "Ac" },
                    { id: "git", label: "Git", icon: "Gi" },
                    { id: "credits", label: "Credits", icon: "Cr" },
                    { id: "onboarding", label: "Onboarding", icon: "On" }
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

            // Bottom: credits balance
            Rectangle {
                Layout.fillWidth: true
                height: 44
                radius: Theme.radiusMd
                color: root.creditsLow ? "#4A2A10" : Theme.panelAlt
                border.color: root.creditsLow ? Theme.warning : Theme.border
                border.width: root.creditsLow ? 1 : 0
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.sp3
                    anchors.rightMargin: Theme.sp3
                    spacing: Theme.sp2
                    Rectangle {
                        width: 8; height: 8; radius: 4
                        color: root.creditsLow ? Theme.warning : Theme.success
                    }
                    Text {
                        text: "Credits"
                        color: root.creditsLow ? Theme.warning : Theme.textMuted
                        font.pixelSize: Theme.fontSizeSm
                    }
                    Text {
                        text: root.compact ? "" : (root.creditsBalance <= 0 ? "$0.00" : ("$" + root.creditsBalance.toFixed(2)))
                        color: Theme.text
                        font.pixelSize: Theme.fontSizeSm
                        font.family: Theme.fontMono
                        font.bold: true
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        visible: !root.compact
                        text: root.creditsLow ? "Low balance" : "Upgrade"
                        color: root.creditsLow ? Theme.warning : Theme.accent
                        font.pixelSize: Theme.fontSizeSm
                        font.bold: true
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.creditsLow ? root.viewSelected("subscription") : root.viewSelected("subscription")
                }
            }

            // Low-balance warning banner
            Rectangle {
                visible: root.creditsLow && !root.compact
                Layout.fillWidth: true
                height: 40
                radius: Theme.radiusMd
                color: "#4A2A10"
                border.color: Theme.warning
                border.width: 1
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.sp3
                    anchors.rightMargin: Theme.sp3
                    spacing: Theme.sp2
                    Text { text: "⚠"; color: Theme.warning; font.pixelSize: Theme.fontSizeMd }
                    Text {
                        text: "Credits are running low. Top up to keep cloud inference active."
                        color: Theme.text
                        font.pixelSize: Theme.fontSizeSm
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Button {
                        text: "Upgrade"
                        font.pixelSize: Theme.fontSizeXs
                        onClicked: root.viewSelected("subscription")
                    }
                }
            }
        }
    }
}
