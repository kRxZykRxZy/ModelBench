import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    anchors.fill: parent

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            anchors.margins: Theme.sp5
            spacing: Theme.sp5

            Text { text: "Subscription"; color: Theme.text; font.pixelSize: Theme.fontSizeXl; font.bold: true }
            Text { text: "Manage your ModelFit plan. Billing happens inside the app."; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm }

            RowLayout {
                spacing: Theme.sp3
                Repeater {
                    model: [
                        { name: "Free", price: "$0", features: "Core local inference" },
                        { name: "Pro", price: "$5/mo", features: "Cloud sync, priority support" },
                        { name: "Ultimate", price: "$12/mo", features: "Advanced features" },
                        { name: "Teams", price: "$29/mo", features: "Team management" }
                    ]
                    delegate: Rectangle {
                        width: 220; height: 260; radius: Theme.radiusLg
                        color: Theme.panel; border.color: Theme.border; border.width: 1
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: Theme.sp4
                            spacing: Theme.sp2
                            Text { text: modelData.name; color: Theme.text; font.pixelSize: Theme.fontSizeLg; font.bold: true; Layout.alignment: Qt.AlignHCenter }
                            Text { text: modelData.price; color: Theme.accent; font.pixelSize: Theme.fontSizeXl; font.bold: true; Layout.alignment: Qt.AlignHCenter }
                            Text { text: modelData.features; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm; wrapMode: Text.Wrap; Layout.fillWidth: true; Layout.alignment: Qt.AlignHCenter }
                            Item { Layout.fillHeight: true }
                            Button {
                                Layout.fillWidth: true
                                text: modelData.name === "Free" ? "Current" : "Upgrade"
                            }
                        }
                    }
                }
            }
        }
    }
}
