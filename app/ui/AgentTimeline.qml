import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Agent timeline: step cards showing tool name, args, output and approval buttons.
// Each step: { id, tool, status ("pending"|"running"|"approved"|"denied"|"complete"),
//   args, output, requiresApproval, ago }
// Emits approveStep(string id) / denyStep(string id) when the user acts on a pending step.
Item {
    id: root
    property var steps: []
    property bool compact: false
    signal approveStep(string stepId)
    signal denyStep(string stepId)

    function statusColor(status) {
        switch (status) {
            case "running": return Theme.info
            case "approved": return Theme.success
            case "denied": return Theme.danger
            case "complete": return Theme.textMuted
            default: return Theme.warning // pending
        }
    }

    function statusLabel(status) {
        switch (status) {
            case "running": return "Running"
            case "approved": return "Approved"
            case "denied": return "Denied"
            case "complete": return "Done"
            default: return "Pending"
        }
    }

    ColumnLayout {
        id: timeline
        anchors.fill: parent
        anchors.margins: Theme.sp3
        spacing: Theme.sp2

        Repeater {
            model: root.steps
            delegate: RowLayout {
                Layout.fillWidth: true
                spacing: Theme.sp3

                // Step number / timeline marker.
                Rectangle {
                    width: 30; height: 30; radius: 15
                    color: modelData.status === "running" ? Theme.panelAlt : Theme.panel
                    border.color: statusColor(modelData.status)
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: index + 1
                        color: statusColor(modelData.status)
                        font.pixelSize: Theme.fontSizeSm
                        font.bold: true
                    }
                }

                // Step card.
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: card.implicitHeight
                    radius: Theme.radiusMd
                    color: modelData.status === "running" ? Theme.panelAlt : Theme.panel
                    border.color: modelData.status === "running" ? Theme.info : (modelData.status === "pending" ? Theme.warning : Theme.border)
                    border.width: 1

                    ColumnLayout {
                        id: card
                        anchors.fill: parent
                        anchors.margins: Theme.sp3
                        spacing: Theme.sp2

                        // Header: tool name, status, time.
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.sp2

                            Text {
                                text: modelData.tool
                                color: Theme.text
                                font.pixelSize: Theme.fontSizeSm
                                font.bold: true
                                font.family: Theme.fontMono
                            }
                            Rectangle {
                                radius: Theme.radiusSm
                                color: statusColor(modelData.status)
                                implicitWidth: 44; implicitHeight: 18
                                Text {
                                    anchors.centerIn: parent
                                    text: statusLabel(modelData.status)
                                    color: "white"
                                    font.pixelSize: Theme.fontSizeXs
                                    font.bold: true
                                }
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: modelData.ago || ""
                                color: Theme.textSubtle
                                font.pixelSize: Theme.fontSizeXs
                            }
                        }

                        // Arguments (collapsible-ish: shown when present).
                        Rectangle {
                            visible: modelData.args && Object.keys(modelData.args).length > 0
                            Layout.fillWidth: true
                            radius: Theme.radiusSm
                            color: Theme.bg
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: Theme.sp2
                                spacing: Theme.sp1

                                Text { text: "Arguments"; color: Theme.textSubtle; font.pixelSize: Theme.fontSizeXs; font.bold: true }
                                Text {
                                    text: JSON.stringify(modelData.args, null, 2)
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.fontSizeXs
                                    font.family: Theme.fontMono
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                            }
                        }

                        // Output.
                        Rectangle {
                            visible: modelData.output !== undefined && modelData.output !== ""
                            Layout.fillWidth: true
                            radius: Theme.radiusSm
                            color: Theme.panelAlt
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: Theme.sp2
                                spacing: Theme.sp1

                                Text { text: "Output"; color: Theme.textSubtle; font.pixelSize: Theme.fontSizeXs; font.bold: true }
                                Text {
                                    text: modelData.output
                                    color: Theme.text
                                    font.pixelSize: Theme.fontSizeSm
                                    font.family: Theme.fontMono
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                            }
                        }

                        // Approval controls for pending steps that require it.
                        RowLayout {
                            visible: modelData.status === "pending" && modelData.requiresApproval
                            Layout.fillWidth: true
                            spacing: Theme.sp2

                            Button {
                                text: "Approve"
                                color: Theme.success
                                font.pixelSize: Theme.fontSizeSm
                                onClicked: root.approveStep(modelData.id)
                            }
                            Button {
                                text: "Deny"
                                color: Theme.danger
                                font.pixelSize: Theme.fontSizeSm
                                onClicked: root.denyStep(modelData.id)
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: "Awaiting approval"
                                color: Theme.warning
                                font.pixelSize: Theme.fontSizeXs
                                font.italic: true
                            }
                        }
                    }
                }
            }
        }

        // Empty state.
        RowLayout {
            visible: root.steps.length === 0
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Text {
                text: "No agent steps yet"
                color: Theme.textSubtle
                font.pixelSize: Theme.fontSizeSm
                font.italic: true
            }
            Item { Layout.fillWidth: true }
        }
    }
}
