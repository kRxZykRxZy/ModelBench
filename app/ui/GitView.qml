import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Git panel: branch switcher, changed-file list, unified diff viewer and commit composer.
// Wires to a future `git` context property; works with sample data until the backend is connected.
Item {
    id: root

    property var branches: ["main", "develop", "feature/agent-timeline"]
    property string currentBranch: "main"
    property var changedFiles: [
        { path: "app/ui/ChatView.qml", status: "modified", additions: 64, deletions: 12 },
        { path: "app/ui/GitView.qml", status: "added", additions: 180, deletions: 0 },
        { path: "app/ui/AgentTimeline.qml", status: "added", additions: 150, deletions: 0 },
        { path: "CMakeLists.txt", status: "modified", additions: 8, deletions: 2 }
    ]
    property string selectedFile: ""
    property var diffLines: []
    property string commitMessage: ""
    property string commitDescription: ""

    function refresh() {
        // TODO(agent-1): replace with git.status() once the git context property exists.
        root.selectedFile = ""
        root.diffLines = []
    }

    function selectFile(file) {
        root.selectedFile = file.path
        // TODO(agent-1): replace with git.diff(file.path).
        var lines = [
            "@@ -10,6 +10,8 @@ import QtQuick.Layouts",
            " Item {",
            "     id: root",
            "     property string currentConvId: \"\"",
            "+    property bool agentMode: false",
            "+    property var agentSteps: []",
            "     property string currentResponse: \"\"",
            "-    property var messages: []",
            "     property var messages: []"
        ]
        root.diffLines = lines
    }

    Component.onCompleted: refresh()

    ColumnLayout {
        id: rootLayout
        anchors.fill: parent
        anchors.margins: Theme.sp3
        spacing: Theme.sp2

        // ---- Header: branch switcher ----
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            radius: Theme.radiusLg
            color: Theme.panel
            border.color: Theme.border
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.sp3
                spacing: Theme.sp2

                Text { text: "Git"; color: Theme.text; font.pixelSize: Theme.fontSizeLg; font.bold: true; font.family: Theme.fontSans }

                Item { Layout.fillWidth: true }

                Text { text: "Branch"; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm }

                ComboBox {
                    id: branchCombo
                    model: root.branches
                    currentIndex: root.branches.indexOf(root.currentBranch)
                    width: 160
                    onActivated: root.currentBranch = branchCombo.currentText
                }

                Button {
                    text: "Switch"
                    enabled: branchCombo.currentText !== root.currentBranch
                    onClicked: { root.currentBranch = branchCombo.currentText; root.refresh() }
                }

                Button {
                    text: "New branch"
                    onClicked: { /* TODO: prompt for branch name */ }
                }

                Button {
                    text: "Refresh"
                    onClicked: root.refresh()
                }
            }
        }

        // ---- Body: files | diff ----
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.sp2

            // Changed files list.
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 300
                radius: Theme.radiusLg
                color: Theme.panel
                border.color: Theme.border
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.sp2
                    spacing: Theme.sp1

                    Text { text: "Changes"; color: Theme.text; font.pixelSize: Theme.fontSizeLg; font.bold: true }

                    ListView {
                        id: fileList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: root.changedFiles
                        delegate: Rectangle {
                            width: fileList.width
                            implicitHeight: 44
                            color: root.selectedFile === modelData.path ? Theme.panelAlt : (index % 2 === 0 ? Theme.panel : "transparent")
                            border.color: Theme.border
                            border.width: 1
                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: Theme.sp2
                                spacing: Theme.sp2

                                Rectangle {
                                    width: 10; height: 10; radius: 5
                                    color: modelData.status === "added" ? Theme.success : (modelData.status === "modified" ? Theme.warning : Theme.danger)
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.path
                                    color: Theme.text
                                    font.pixelSize: Theme.fontSizeSm
                                    elide: Text.ElideMiddle
                                }

                                Text {
                                    text: "+" + modelData.additions + " / -" + modelData.deletions
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.fontSizeXs
                                    font.family: Theme.fontMono
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.selectFile(modelData)
                                }
                            }
                        }
                    }
                }
            }

            // Diff viewer.
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: Theme.radiusLg
                color: Theme.panel
                border.color: Theme.border
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.sp2
                    spacing: Theme.sp1

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: root.selectedFile ? root.selectedFile : "Select a file to view its diff"
                            color: root.selectedFile ? Theme.text : Theme.textMuted
                            font.pixelSize: Theme.fontSizeSm
                            font.bold: true
                            font.family: Theme.fontMono
                        }
                        Item { Layout.fillWidth: true }
                        Button {
                            text: "Stage all"
                            enabled: root.changedFiles.length > 0
                            font.pixelSize: Theme.fontSizeXs
                            onClicked: { /* TODO: git.stageAll() */ }
                        }
                    }

                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true

                        ListView {
                            id: diffList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: root.diffLines
                            delegate: Item {
                                width: diffList.width
                                implicitHeight: 20
                                RowLayout {
                                    anchors.fill: parent
                                    spacing: 0

                                    Rectangle {
                                        width: 24
                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            text: modelData.startsWith("+") ? "+" : (modelData.startsWith("-") ? "-" : " ")
                                            color: modelData.startsWith("+") ? Theme.success : (modelData.startsWith("-") ? Theme.danger : Theme.borderStrong)
                                            font.pixelSize: Theme.fontSizeSm
                                            font.family: Theme.fontMono
                                            font.bold: true
                                        }
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData
                                        color: modelData.startsWith("+") ? Theme.success : (modelData.startsWith("-") ? Theme.danger : Theme.textMuted)
                                        font.pixelSize: Theme.fontSizeSm
                                        font.family: Theme.fontMono
                                        wrapMode: Text.NoWrap
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ---- Commit composer ----
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 150
            radius: Theme.radiusLg
            color: Theme.bgElevated
            border.color: Theme.border
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.sp3
                spacing: Theme.sp2

                Text { text: "Commit"; color: Theme.text; font.pixelSize: Theme.fontSizeLg; font.bold: true }

                TextField {
                    Layout.fillWidth: true
                    placeholderText: "Commit message (e.g. feat: add agent timeline UI)"
                    text: root.commitMessage
                    onTextChanged: root.commitMessage = text
                    font.pixelSize: Theme.fontSizeMd
                    color: Theme.text
                }

                TextArea {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 60
                    placeholderText: "Description..."
                    text: root.commitDescription
                    onTextChanged: root.commitDescription = text
                    wrapMode: TextArea.Wrap
                    font.pixelSize: Theme.fontSizeSm
                    color: Theme.text
                    background: Rectangle { color: "transparent" }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp2

                    Item { Layout.fillWidth: true }

                    Button {
                        text: "Cancel"
                        font.pixelSize: Theme.fontSizeSm
                        onClicked: { root.commitMessage = ""; root.commitDescription = "" }
                    }

                    Button {
                        text: "Commit"
                        font.pixelSize: Theme.fontSizeSm
                        enabled: root.commitMessage.trim().length > 0
                        palette.button: Theme.accent
                        palette.buttonText: "white"
                        onClicked: {
                            // TODO(agent-1): git.commit(root.commitMessage, root.commitDescription)
                            root.commitMessage = ""
                            root.commitDescription = ""
                            root.refresh()
                        }
                    }
                }
            }
        }
    }
}
