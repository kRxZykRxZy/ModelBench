import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Onboarding wizard: hardware -> engine -> models -> account -> ready.
// Emits finished() when the user completes the final step.
Item {
    id: root
    signal finished()

    property int currentStep: 0
    readonly property int stepCount: 5
    readonly property var stepNames: ["Hardware", "Engine", "Models", "Account", "Ready"]
    property bool completed: false

    function goNext() {
        if (root.currentStep < root.stepCount - 1) root.currentStep++
        else root.finished()
    }
    function goBack() { if (root.currentStep > 0) root.currentStep-- }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.sp4
        spacing: Theme.sp3

        // Header.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.sp1
            Text {
                text: "Welcome to ModelBench"
                color: Theme.text
                font.pixelSize: Theme.fontSize2xl
                font.bold: true
                font.family: Theme.fontSans
                Layout.alignment: Qt.AlignHCenter
            }
            Text {
                text: stepNames[root.currentStep]
                color: Theme.accent
                font.pixelSize: Theme.fontSizeMd
                font.bold: true
                font.family: Theme.fontSans
                Layout.alignment: Qt.AlignHCenter
            }
            Text {
                text: "Step " + (root.currentStep + 1) + " of " + stepCount
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeXs
                Layout.alignment: Qt.AlignHCenter
            }
        }

        // Stepper.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.sp2
            Repeater {
                model: stepCount
                delegate: ColumnLayout {
                    spacing: Theme.sp1
                    Rectangle {
                        width: 30; height: 30; radius: 15
                        color: index <= root.currentStep ? Theme.accent : Theme.border
                        Text {
                            anchors.centerIn: parent
                            text: index + 1
                            color: index <= root.currentStep ? "white" : Theme.textMuted
                            font.pixelSize: Theme.fontSizeSm
                            font.bold: true
                        }
                    }
                    Text {
                        text: stepNames[index]
                        color: index <= root.currentStep ? Theme.text : Theme.textMuted
                        font.pixelSize: Theme.fontSizeXs
                        wrapMode: Text.Wrap
                        horizontalAlignment: Text.AlignHCenter
                        width: 70
                    }
                }
            }
        }

        // Pages.
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.currentStep

            // --- Hardware ---
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Theme.sp3
                Item { Layout.fillHeight: true }
                ColumnLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: Theme.sp2
                    Text { text: "🖥️"; font.pixelSize: 32; Layout.alignment: Qt.AlignHCenter }
                    Text { text: "Detected Hardware"; color: Theme.text; font.pixelSize: Theme.fontSizeLg; font.bold: true; Layout.alignment: Qt.AlignHCenter }
                    Text { text: "We detected your system so ModelBench can pick the right inference backend."; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter; width: 340; Layout.alignment: Qt.AlignHCenter }
                    Rectangle {
                        Layout.preferredWidth: 360; implicitHeight: 160
                        radius: Theme.radiusLg; color: Theme.panel; border.color: Theme.border; border.width: 1
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: Theme.sp3; spacing: Theme.sp1
                            Text { text: "CPU: " + (app.hardwareInfo().cpuModel || "Unknown"); color: Theme.text; font.pixelSize: Theme.fontSizeSm }
                            Text { text: "Cores: " + (app.hardwareInfo().cpuCores || 0) + "  ·  Threads: " + (app.hardwareInfo().cpuThreads || 0); color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm }
                            Text { text: "RAM: " + Math.round((app.hardwareInfo().ramBytes || 0) / 1073741824) + " GB"; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm }
                            Text { text: "GPU: " + (app.hardwareInfo().gpuModel || "Unknown"); color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm }
                            Text { text: "Backends: " + ((app.hardwareInfo().supportedBackends || []).join(", ")); color: Theme.textSubtle; font.pixelSize: Theme.fontSizeXs; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        }
                    }
                }
                Item { Layout.fillHeight: true }
            }

            // --- Engine ---
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Theme.sp3
                Item { Layout.fillHeight: true }
                ColumnLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: Theme.sp2
                    Text { text: "⚙️"; font.pixelSize: 32; Layout.alignment: Qt.AlignHCenter }
                    Text { text: "Inference Engine"; color: Theme.text; font.pixelSize: Theme.fontSizeLg; font.bold: true; Layout.alignment: Qt.AlignHCenter }
                    Text { text: "llama.cpp powers local inference. Install it from GitHub releases."; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter; width: 340; Layout.alignment: Qt.AlignHCenter }
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: Theme.sp2
                        Button {
                            text: engineInstaller.getIsInstalled() ? "Reinstall" : "Install"
                            onClicked: engineInstaller.install()
                        }
                        Button { text: "Skip"; onClicked: root.goNext() }
                    }
                    Text { text: engineInstaller.getIsInstalled() ? ("Installed: " + engineInstaller.getInstalledVersion()) : "Not installed"; color: engineInstaller.getIsInstalled() ? Theme.success : Theme.warning; font.pixelSize: Theme.fontSizeSm; Layout.alignment: Qt.AlignHCenter }
                }
                Item { Layout.fillHeight: true }
            }

            // --- Models ---
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Theme.sp3
                Item { Layout.fillHeight: true }
                ColumnLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: Theme.sp2
                    Text { text: "📦"; font.pixelSize: 32; Layout.alignment: Qt.AlignHCenter }
                    Text { text: "Models"; color: Theme.text; font.pixelSize: Theme.fontSizeLg; font.bold: true; Layout.alignment: Qt.AlignHCenter }
                    Text { text: "Download a GGUF model from Hugging Face to start chatting locally."; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter; width: 340; Layout.alignment: Qt.AlignHCenter }
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: Theme.sp2
                        Button { text: "Open Model Library"; onClicked: Navigation.go("models") }
                        Button { text: "Skip"; onClicked: root.goNext() }
                    }
                }
                Item { Layout.fillHeight: true }
            }

            // --- Account ---
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Theme.sp3
                Item { Layout.fillHeight: true }
                ColumnLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: Theme.sp2
                    Text { text: "👤"; font.pixelSize: 32; Layout.alignment: Qt.AlignHCenter }
                    Text { text: "Account"; color: Theme.text; font.pixelSize: Theme.fontSizeLg; font.bold: true; Layout.alignment: Qt.AlignHCenter }
                    Text { text: "Sign in to sync entitlements and credits across devices. Core local inference stays free."; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter; width: 340; Layout.alignment: Qt.AlignHCenter }
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: Theme.sp2
                        Button { text: "Sign in"; onClicked: Navigation.go("account") }
                        Button { text: "Skip"; onClicked: root.goNext() }
                    }
                }
                Item { Layout.fillHeight: true }
            }

            // --- Ready ---
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Theme.sp3
                Item { Layout.fillHeight: true }
                ColumnLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: Theme.sp3
                    Text { text: "✅"; font.pixelSize: 40; Layout.alignment: Qt.AlignHCenter }
                    Text { text: "You're all set"; color: Theme.text; font.pixelSize: Theme.fontSizeLg; font.bold: true; Layout.alignment: Qt.AlignHCenter }
                    Text { text: "Start a new chat or explore the Model Library."; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter; width: 340; Layout.alignment: Qt.AlignHCenter }
                    Button {
                        text: "Finish"
                        palette.button: Theme.accent
                        palette.buttonText: "white"
                        font.bold: true
                        onClicked: root.finished()
                    }
                }
                Item { Layout.fillHeight: true }
            }
        }

        // Footer navigation.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.sp2
            Button {
                text: "Back"
                enabled: root.currentStep > 0
                font.pixelSize: Theme.fontSizeSm
                onClicked: root.goBack()
            }
            Item { Layout.fillWidth: true }
            Button {
                text: root.currentStep === root.stepCount - 1 ? "Finish" : "Next"
                palette.button: Theme.accent
                        palette.buttonText: "white"
                font.bold: true
                font.pixelSize: Theme.fontSizeSm
                onClicked: root.goNext()
            }
        }
    }
}
