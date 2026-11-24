import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Credits page: balance display, low-balance warning and upgrade prompt.
// Reads from the `credits` context property (balance/currency) exposed by the backend; falls back gracefully.
Item {
    id: root
    property int balance: (credits && credits.balance !== undefined) ? credits.balance : 0
    property string currency: (credits && credits.currency !== undefined) ? credits.currency : "USD"
    property int lowThreshold: 10
    property bool lowBalance: root.balance > 0 && root.balance < root.lowThreshold

    function formattedBalance() {
        if (root.balance <= 0) return "0.00"
        var v = root.balance / 100
        return v.toFixed(2)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.sp5
        spacing: Theme.sp4

        Text { text: "Credits"; color: Theme.text; font.pixelSize: Theme.fontSizeXl; font.bold: true }
        Text { text: "Credits are used for cloud AI inference. Local inference is always free."; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm; wrapMode: Text.Wrap; Layout.fillWidth: true }

        // Balance card.
        Rectangle {
            Layout.fillWidth: true
            radius: Theme.radiusLg
            color: Theme.panel
            border.color: root.lowBalance ? Theme.warning : Theme.border
            border.width: root.lowBalance ? 2 : 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.sp5
                spacing: Theme.sp3

                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "Balance"; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: root.currency + " " + root.formattedBalance()
                        color: root.lowBalance ? Theme.warning : Theme.text
                        font.pixelSize: Theme.fontSizeXl
                        font.bold: true
                        font.family: Theme.fontMono
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "Low balance warning"; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm }
                    Item { Layout.fillWidth: true }
                    Rectangle {
                        width: 10; height: 10; radius: 5
                        color: root.lowBalance ? Theme.warning : Theme.success
                    }
                    Text {
                        text: root.lowBalance ? "Low — top up soon" : "Healthy"
                        color: root.lowBalance ? Theme.warning : Theme.success
                        font.pixelSize: Theme.fontSizeSm
                        font.bold: true
                    }
                }

                Item { Layout.fillHeight: true }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp2
                    Button {
                        Layout.fillWidth: true
                        text: "Add credits"
                        onClicked: Navigation.go("subscription")
                    }
                    Button {
                        Layout.fillWidth: true
                        text: root.lowBalance ? "Upgrade now" : "View plans"
                        palette.button: Theme.accent
                        palette.buttonText: "white"
                        onClicked: Navigation.go("subscription")
                    }
                }

                Text {
                    visible: root.lowBalance
                    text: "Your balance is running low. Cloud inference will be paused until you add credits."
                    color: Theme.warning
                    font.pixelSize: Theme.fontSizeSm
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }
        }

        // Usage history placeholder.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 200
            radius: Theme.radiusLg
            color: Theme.panel
            border.color: Theme.border
            border.width: 1
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.sp4
                Text { text: "Recent usage"; color: Theme.text; font.pixelSize: Theme.fontSizeMd; font.bold: true }
                Item { Layout.fillHeight: true }
                Text { text: "No usage yet."; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm; font.italic: true; Layout.alignment: Qt.AlignHCenter }
                Item { Layout.fillHeight: true }
            }
        }
    }
}
