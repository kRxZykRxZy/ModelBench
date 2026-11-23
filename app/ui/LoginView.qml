import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property string statusText: ""
    property bool loggingIn: false
    property string errorMessage: ""
    property string successMessage: ""

    function attemptLogin() {
        root.loggingIn = true
        root.errorMessage = ""
        root.successMessage = ""
        root.statusText = "Signing in..."
        account.login(emailField.text, passField.text)
    }
    function attemptSignup() {
        root.loggingIn = true
        root.errorMessage = ""
        root.successMessage = ""
        root.statusText = "Creating account..."
        account.signup(emailField.text, passField.text)
    }

    ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: Theme.sp5
        spacing: Theme.sp4
        Layout.alignment: Qt.AlignVCenter

        Text { text: "ModelBench Account"; color: Theme.text; font.pixelSize: Theme.fontSize2xl; font.bold: true; Layout.alignment: Qt.AlignHCenter }
        Text { text: "Sign in or create an account to sync entitlements."; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm; Layout.alignment: Qt.AlignHCenter; wrapMode: Text.Wrap; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter }

        // Error banner
        Rectangle {
            visible: root.errorMessage.length > 0
            Layout.fillWidth: true
            radius: Theme.radiusMd
            color: "#5A1A1A"
            border.color: Theme.danger
            border.width: 1
            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.sp3
                spacing: Theme.sp2
                Text { text: "✖"; color: Theme.danger; font.pixelSize: Theme.fontSizeMd }
                Text {
                    text: root.errorMessage
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeSm
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                Button {
                    text: "Dismiss"
                    font.pixelSize: Theme.fontSizeXs
                    onClicked: root.errorMessage = ""
                }
            }
        }

        // Success banner
        Rectangle {
            visible: root.successMessage.length > 0
            Layout.fillWidth: true
            radius: Theme.radiusMd
            color: "#1A3A1A"
            border.color: Theme.success
            border.width: 1
            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.sp3
                spacing: Theme.sp2
                Text { text: "✔"; color: Theme.success; font.pixelSize: Theme.fontSizeMd }
                Text {
                    text: root.successMessage
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeSm
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }
        }

        // Status line
        Text { text: root.statusText; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm; Layout.alignment: Qt.AlignHCenter; wrapMode: Text.Wrap; Layout.fillWidth: true }

        GroupBox {
            title: "Server"
            Layout.fillWidth: true
            enabled: !root.loggingIn
            RowLayout {
                width: parent.width
                TextField { id: serverUrl; Layout.fillWidth: true; text: account.serverUrl; placeholderText: "https://your-site-url.example.com"; color: Theme.text }
                Button { text: "Set"; enabled: !root.loggingIn; onClicked: { account.serverUrl = serverUrl.text; root.statusText = "Server URL set" } }
            }
        }

        GroupBox {
            title: "Login / Signup"
            Layout.fillWidth: true
            enabled: !root.loggingIn
            GridLayout {
                width: parent.width
                columns: 2
                columnSpacing: Theme.sp3
                rowSpacing: Theme.sp2
                Text { text: "Email"; color: Theme.textMuted }
                TextField { id: emailField; Layout.fillWidth: true; color: Theme.text; placeholderText: "you@example.com" }
                Text { text: "Password"; color: Theme.textMuted }
                TextField { id: passField; Layout.fillWidth: true; echoMode: TextInput.Password; color: Theme.text; placeholderText: "••••••••" }
            }
            RowLayout {
                Layout.fillWidth: true
                Button { text: "Login"; Layout.fillWidth: true; enabled: !root.loggingIn && emailField.text.trim().length > 0 && passField.text.trim().length > 0; onClicked: root.attemptLogin() }
                Button { text: "Sign Up"; Layout.fillWidth: true; enabled: !root.loggingIn && emailField.text.trim().length > 0 && passField.text.trim().length > 0; onClicked: root.attemptSignup() }
            }
            Button { text: "Logout"; enabled: account.loggedIn && !root.loggingIn; onClicked: account.logout() }
        }

        // Busy indicator during auth
        BusyIndicator {
            visible: root.loggingIn
            running: root.loggingIn
            Layout.alignment: Qt.AlignHCenter
        }

        Text { text: account.loggedIn ? ("Logged in: " + account.email + " (" + account.tier + ")") : "Not logged in"; color: account.loggedIn ? Theme.success : Theme.textMuted; font.pixelSize: Theme.fontSizeSm; Layout.alignment: Qt.AlignHCenter }

        Connections {
            target: account
            function onAuthError(m) {
                root.loggingIn = false
                root.errorMessage = m
                root.statusText = ""
            }
            function onAuthSuccess(m) {
                root.loggingIn = false
                root.successMessage = m
                root.statusText = ""
            }
        }
    }
}
