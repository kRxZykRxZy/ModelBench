import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property int selectedPlan: 0

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.sp5
        spacing: Theme.sp4

        Text { text: "Subscription"; color: Theme.text; font.pixelSize: Theme.fontSizeXl; font.bold: true }
        Text { text: "Current plan: " + subscription.tierName() + "  ·  Core local inference is always free."; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm; wrapMode: Text.Wrap; Layout.fillWidth: true }

        GridLayout {
            Layout.fillWidth: true
            columns: 4
            columnSpacing: Theme.sp3
            Repeater {
                model: subscription.planNames()
                delegate: Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 260
                    radius: Theme.radiusLg
                    color: root.selectedPlan === index ? Theme.panelAlt : Theme.panel
                    border.color: root.selectedPlan === index ? Theme.accent : Theme.border
                    border.width: 1
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.sp4
                        spacing: Theme.sp2
                        Text { text: modelData; color: Theme.text; font.pixelSize: Theme.fontSizeLg; font.bold: true; Layout.alignment: Qt.AlignHCenter }
                        Text { text: subscription.planPrices()[index]; color: Theme.accent; font.pixelSize: Theme.fontSizeXl; font.bold: true; Layout.alignment: Qt.AlignHCenter }
                        Item { Layout.fillHeight: true }
                        Button {
                            Layout.fillWidth: true
                            text: index <= subscription.currentTier() ? "Current" : "Upgrade"
                            enabled: index > subscription.currentTier()
                            onClicked: {
                                root.selectedPlan = index
                                subscription.startCheckoutTier(index)
                                toast.text = "Checkout requires a configured secure billing backend. Premium features stay locked offline."
                            }
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        z: -1
                        onClicked: root.selectedPlan = index
                    }
                }
            }
        }

        Text { id: toast; text: ""; color: Theme.warning; font.pixelSize: Theme.fontSizeSm; wrapMode: Text.Wrap; Layout.fillWidth: true }
    }
}
