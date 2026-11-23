import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property string statusText: ""

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.sp5
        spacing: Theme.sp4
        Layout.alignment: Qt.AlignVCenter

        Text { text: "ModelBench Account"; color: Theme.text; font.pixelSize: Theme.fontSize2xl; font.bold: true; Layout.alignment: Qt.AlignHCenter }
        Text { text: "Sign in or create an account to sync entitlements."; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm; Layout.alignment: Qt.AlignHCenter; wrapMode: Text.Wrap; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter }

        GroupBox {
            title: "Server"
            Layout.fillWidth: true
            RowLayout {
                width: parent.width
                TextField { id: serverUrl; Layout.fillWidth: true; text: account.serverUrl; placeholderText: "https://your-site-url.example.com"; color: Theme.text }
                Button { text: "Set"; onClicked: { account.serverUrl = serverUrl.text; root.statusText = "Server URL set" } }
            }
        }

        GroupBox {
            title: "Login / Signup"
            Layout.fillWidth: true
            GridLayout {
                width: parent.width
                columns: 2
                columnSpacing: Theme.sp3
                rowSpacing: Theme.sp2
                Text { text: "Email"; color: Theme.textMuted }
                TextField { id: emailField; Layout.fillWidth: true; color: Theme.text }
                Text { text: "Password"; color: Theme.textMuted }
                TextField { id: passField; Layout.fillWidth: true; echoMode: TextInput.Password; color: Theme.text }
            }
            RowLayout {
                Layout.fillWidth: true
                Button { text: "Login"; Layout.fillWidth: true; onClicked: account.login(emailField.text, passField.text) }
                Button { text: "Sign Up"; Layout.fillWidth: true; onClicked: account.signup(emailField.text, passField.text) }
            }
            Button { text: "Logout"; enabled: account.loggedIn; onClicked: account.logout() }
        }

        Text { text: account.loggedIn ? ("Logged in: " + account.email + " (" + account.tier + ")") : "Not logged in"; color: account.loggedIn ? Theme.success : Theme.textMuted; font.pixelSize: Theme.fontSizeSm; Layout.alignment: Qt.AlignHCenter }
        Text { text: root.statusText; color: Theme.warning; font.pixelSize: Theme.fontSizeSm; Layout.alignment: Qt.AlignHCenter; wrapMode: Text.Wrap; Layout.fillWidth: true }

        Connections {
            target: account
            function onAuthError(m) { root.statusText = m }
            function onAuthSuccess(m) { root.statusText = m }
        }
    }
}
