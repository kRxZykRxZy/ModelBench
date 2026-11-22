import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    function refresh() { skillList.model = skills.getInstalled() }
    Component.onCompleted: refresh()
    Connections { target: skills; function onSkillInstalled() { root.refresh() } function onSkillUninstalled() { root.refresh() } }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.sp5
        spacing: Theme.sp4

        Text { text: "Skills Hub"; color: Theme.text; font.pixelSize: Theme.fontSizeXl; font.bold: true }
        Text { text: "Skills are reusable instruction packages for AI agents. Installed skills are stored locally."; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm; wrapMode: Text.Wrap; Layout.fillWidth: true }

        GroupBox {
            title: "Install a Skill"
            Layout.fillWidth: true
            RowLayout {
                width: parent.width
                TextField { id: skillPath; Layout.fillWidth: true; placeholderText: "C:\\path\\to\\skill-folder"; color: Theme.text }
                Button {
                    text: "Install"
                    onClicked: {
                        if (skillPath.text.trim()) {
                            skills.installSkill(skillPath.text.trim())
                            skillPath.text = ""
                            statusText.text = "Skill installed"
                        }
                    }
                }
            }
        }

        Text { id: statusText; text: ""; color: Theme.success; font.pixelSize: Theme.fontSizeSm }

        Text { text: "Installed Skills"; color: Theme.text; font.bold: true; font.pixelSize: Theme.fontSizeLg }
        ListView {
            id: skillList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: []
            delegate: Rectangle {
                width: skillList.width
                implicitHeight: 70
                color: "transparent"
                border.color: Theme.border
                border.width: 1
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.sp3
                    ColumnLayout {
                        Layout.fillWidth: true
                        Text { text: modelData.name || modelData.id; color: Theme.text; font.bold: true; font.pixelSize: Theme.fontSizeMd }
                        Text { text: modelData.description || ""; color: Theme.textMuted; font.pixelSize: Theme.fontSizeXs; elide: Text.ElideRight; Layout.fillWidth: true }
                    }
                    Switch {
                        checked: modelData.enabled
                        onToggled: skills.setSkillEnabled(modelData.id, checked)
                    }
                    Button { text: "Remove"; onClicked: { skills.uninstallSkill(modelData.id); statusText.text = "Removed" } }
                }
            }
        }
    }
}
