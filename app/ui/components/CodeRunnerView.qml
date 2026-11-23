import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Scratchpad for compiling and running a snippet of C++, Python or JavaScript.
// Wire-up: expose ModelBench::CodeRunner as the context property `codeRunner`
// in Application::registerQmlTypes(). The typeof guard keeps the component
// loadable in tooling when the backend is not registered.
Item {
    id: root

    property var runner: (typeof codeRunner !== "undefined") ? codeRunner : null
    property var clipboardBackend: (typeof clipboard !== "undefined") ? clipboard : null
    property var notifierBackend: (typeof notifier !== "undefined") ? notifier : null
    property string language: "cpp"
    property string code: root.sample("cpp")
    property string outputText: ""
    property var lastResult: ({})
    property bool running: false

    property var languages: root.runner ? root.runner.availableLanguages()
                                        : ["cpp", "python", "javascript"]
    property string command: root.runner ? root.runner.commandFor(root.language) : ""

    function sample(lang) {
        if (lang === "python") return "print('Hello from ModelBench')\n";
        if (lang === "javascript") return "console.log('Hello from ModelBench');\n";
        return "#include <iostream>\n" +
               "int main() {\n" +
               "    std::cout << \"Hello from ModelBench\\n\";\n" +
               "    return 0;\n" +
               "}\n";
    }

    function appendOut(stream, chunk) {
        var prefix = stream === "compile" ? "[build] "
                     : (stream === "stderr" ? "[err] " : "");
        root.outputText += prefix + chunk;
        if (root.outputText.length > 200000)
            root.outputText = root.outputText.substring(root.outputText.length - 200000);
    }

    function execute() {
        root.outputText = "";
        root.lastResult = ({});
        if (root.runner) {
            root.runner.run(root.language, root.code, "");
        } else {
            root.appendOut("stderr",
                           "Code runner backend unavailable: expose ModelBench::CodeRunner " +
                           "as the context property 'codeRunner'.\n");
        }
    }

    function loadSample() { root.code = root.sample(root.language); }

    onOutputTextChanged: Qt.callLater(function() {
        outputFlick.contentY = Math.max(0, outputFlick.contentHeight - outputFlick.height);
    })

    Connections {
        target: root.runner
        function onOutput(stream, chunk) { root.appendOut(stream, chunk); }
        function onRunningChanged(value) { root.running = value; }
        function onErrorOccurred(message) { root.appendOut("stderr", message + "\n"); }
        function onFinished(result) {
            root.lastResult = result;
            root.running = false;
            if (result.compileOutput)
                root.appendOut("compile", result.compileOutput);
            var status = result.ok
                         ? "OK"
                         : "FAILED: " + (result.error || ("exit " + result.exitCode));
            root.appendOut("stdout", "\n-- " + status + " (" + result.durationMs +
                                     " ms) --\n");
            if (result.truncated)
                root.appendOut("stderr", "Output was truncated.\n");
            if (root.notifierBackend)
                var name = root.runner ? root.runner.displayName(root.language)
                                       : root.language;
                root.notifierBackend.notify("ModelBench · " + name, status, 4000);
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.sp3
        spacing: Theme.sp2

        // ---- Toolbar ----
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

                Text {
                    text: "Run code"
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeLg
                    font.bold: true
                    font.family: Theme.fontSans
                }

                ComboBox {
                    id: languageCombo
                    model: root.languages
                    width: 160
                    onActivated: {
                        root.language = root.languages[index];
                        root.loadSample();
                    }
                    delegate: ItemDelegate {
                        width: languageCombo.width
                        text: root.runner ? root.runner.displayName(modelData)
                                          : modelData
                        highlighted: languageCombo.highlightedIndex === index
                    }
                    contentItem: Text {
                        text: root.runner ? root.runner.displayName(languageCombo.currentText)
                                          : languageCombo.currentText
                        color: Theme.text
                        font.family: Theme.fontSans
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                }

                Text {
                    text: root.command ? ("using: " + root.command)
                                       : "not installed"
                    color: root.command ? Theme.textMuted : Theme.warning
                    font.pixelSize: Theme.fontSizeSm
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }

                Button {
                    text: "Sample"
                    onClicked: root.loadSample()
                }
                Button {
                    text: root.running ? "Stop" : "Run"
                    enabled: root.running || root.command !== ""
                    highlighted: !root.running
                    onClicked: {
                        if (root.running) {
                            if (root.runner) root.runner.cancel();
                        } else {
                            root.execute();
                        }
                    }
                }
            }
        }

        // ---- Source / output ----
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.sp2

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 320
                radius: Theme.radiusLg
                color: Theme.panel
                border.color: Theme.border
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.sp2
                    spacing: Theme.sp1

                    Text {
                        text: "SOURCE · " + root.language.toUpperCase()
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSizeSm
                        font.bold: true
                    }
                    TextArea {
                        id: editor
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        text: root.code
                        onTextChanged: root.code = editor.text
                        color: Theme.text
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontSizeMd
                        selectByMouse: true
                        wrapMode: TextEdit.NoWrap
                        background: Rectangle {
                            color: Theme.bg
                            border.color: Theme.border
                            radius: Theme.radiusSm
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 320
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
                            text: "OUTPUT"
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSizeSm
                            font.bold: true
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: root.running ? "running…" : ""
                            color: Theme.accent
                            font.pixelSize: Theme.fontSizeSm
                        }
                        Button {
                            text: "Copy"
                            onClicked: {
                                if (root.clipboardBackend && root.outputText.length > 0)
                                    root.clipboardBackend.setText(root.outputText);
                            }
                        }
                        Button {
                            text: "Clear"
                            onClicked: root.outputText = ""
                        }
                    }

                    Flickable {
                        id: outputFlick
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        contentWidth: width
                        contentHeight: outText.height
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds

                        TextEdit {
                            id: outText
                            width: outputFlick.width
                            text: root.outputText
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.Wrap
                            color: Theme.text
                            font.family: Theme.fontMono
                            font.pixelSize: Theme.fontSizeMd
                        }
                    }
                }
            }
        }
    }
}
