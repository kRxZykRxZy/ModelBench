import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Embedded interactive terminal.
//
// Wire-up: expose the C++ ModelBench::TerminalSession as a context property
// named `terminalSession` (see Application::registerQmlTypes). The `typeof`
// guard below lets this component still load when that property is absent, so
// tooling such as qmllint / the QML preview does not fail.
//
// A compact ANSI SGR parser renders colours and bold text as rich text; other
// escape sequences (cursor movement, OSC titles) are consumed and ignored.
Item {
    id: root

    property var session: (typeof terminalSession !== "undefined") ? terminalSession : null
    property var clipboardBackend: (typeof clipboard !== "undefined") ? clipboard : null
    property string startDirectory: ""
    property int fontSize: Theme.fontSizeMd
    property int maxLines: 800
    property bool sessionRunning: false

    property var _lines: []
    property string _currentLine: ""
    property var _history: []
    property int _historyIndex: 0

    function escChar(c) {
        if (c === "&") return "&amp;";
        if (c === "<") return "&lt;";
        if (c === ">") return "&gt;";
        return c;
    }

    function applySgr(seq) {
        var codes = seq.length ? seq.split(";") : ["0"];
        for (var i = 0; i < codes.length; ++i) {
            var code = parseInt(codes[i] === "" ? "0" : codes[i], 10);
            if (isNaN(code)) code = 0;
            if (code === 0) { ansi.fg = ""; ansi.bg = ""; ansi.bold = false; }
            else if (code === 1) ansi.bold = true;
            else if (code === 22) ansi.bold = false;
            else if (code === 39) ansi.fg = "";
            else if (code === 49) ansi.bg = "";
            else if (code >= 30 && code <= 37) ansi.fg = ansi.palette[code - 30];
            else if (code >= 90 && code <= 97) ansi.fg = ansi.bright[code - 90];
            else if (code >= 40 && code <= 47) ansi.bg = ansi.palette[code - 40];
            else if (code >= 100 && code <= 107) ansi.bg = ansi.bright[code - 100];
        }
    }

    // Converts a chunk of terminal bytes (already decoded to text by the
    // backend) into HTML lines, carrying SGR state across chunk boundaries.
    function appendText(text) {
        if (!text || text.length === 0) return;
        var completed = [];
        var current = "";
        var i = 0;

        function closeSpan() {
            if (ansi.openTag !== "") { current += "</span>"; ansi.openTag = ""; }
        }
        function ensureSpan() {
            var tag = "";
            if (ansi.fg !== "" || ansi.bg !== "" || ansi.bold) {
                var style = "";
                if (ansi.fg !== "") style += "color:" + ansi.fg + ";";
                if (ansi.bg !== "") style += "background-color:" + ansi.bg + ";";
                if (ansi.bold) style += "font-weight:bold;";
                tag = "<span style=\"" + style + "\">";
            }
            if (tag !== ansi.openTag) {
                closeSpan();
                if (tag !== "") { current += tag; ansi.openTag = tag; }
            }
        }

        while (i < text.length) {
            var ch = text.charAt(i);
            if (ch === "\u001b") {
                var next = text.charAt(i + 1);
                if (next === "[") { // CSI
                    var j = i + 2;
                    while (j < text.length && !/[A-Za-z]/.test(text.charAt(j))) ++j;
                    var seq = text.substring(i + 2, j);
                    var cmd = j < text.length ? text.charAt(j) : "";
                    if (cmd === "m") root.applySgr(seq);
                    i = j < text.length ? j + 1 : j;
                    continue;
                }
                if (next === "]") { // OSC (window title etc.)
                    var k = i + 2;
                    while (k < text.length && text.charCodeAt(k) !== 7 &&
                           text.charCodeAt(k) !== 27) ++k;
                    i = k < text.length ? k + 1 : k;
                    continue;
                }
                i += 2; // two-byte escape
                continue;
            }
            if (ch === "\r") { // carriage return: overwrite the current line
                current = "";
                closeSpan();
                ++i;
                continue;
            }
            if (ch === "\n") {
                closeSpan();
                completed.push(current);
                current = "";
                ++i;
                continue;
            }
            if (ch === "\b") {
                current = current.substring(0, Math.max(0, current.length - 1));
                ++i;
                continue;
            }
            ensureSpan();
            current += root.escChar(ch);
            ++i;
        }

        if (completed.length > 0) {
            var all = root._lines.concat(completed);
            if (all.length > root.maxLines) all.splice(0, all.length - root.maxLines);
            root._lines = all;
        }
        root._currentLine = current;
    }

    function displayText() {
        var body = root._lines.join("\n");
        if (root._lines.length > 0) body += "\n";
        body += root._currentLine;
        return "<pre style=\"white-space:pre; font-family:'" + Theme.fontMono +
               "'; font-size:" + root.fontSize + "px;\">" + body + "</pre>";
    }

    function clear() {
        root._lines = [];
        root._currentLine = "";
    }

    function copySelection() {
        var text = outputText.selectedText;
        if (!text || text.length === 0) return;
        if (root.clipboardBackend) root.clipboardBackend.setText(text);
    }

    function syncSize() {
        if (!root.session || !root.sessionRunning) return;
        var charWidth = fontMetrics.averageCharacterWidth;
        var lineHeight = fontMetrics.height * 1.2;
        if (charWidth <= 0 || lineHeight <= 0) return;
        var cols = Math.max(20, Math.min(240, Math.floor(outputView.width / charWidth)));
        var rows = Math.max(4, Math.min(200, Math.floor(outputView.height / lineHeight)));
        root.session.resize(cols, rows);
    }

    property string html: root.displayText()
    onHtmlChanged: Qt.callLater(function() {
        outputView.contentY = Math.max(0, outputView.contentHeight - outputView.height);
    })

    QtObject {
        id: ansi
        property string fg: ""
        property string bg: ""
        property bool bold: false
        property string openTag: ""
        property var palette: ["#1e1e1e", "#cd3131", "#0dbc79", "#e5e510",
                               "#2472c8", "#bc3fbc", "#11a8cd", "#e5e5e5"]
        property var bright: ["#666666", "#f14c4c", "#23d18b", "#f5f543",
                              "#3b8eea", "#d670d6", "#29b8db", "#ffffff"]
    }

    FontMetrics {
        id: fontMetrics
        font.family: Theme.fontMono
        font.pixelSize: root.fontSize
    }

    Connections {
        target: root.session
        function onDataReceived(text) { root.appendText(text); }
        function onProcessFinished(code) {
            root.appendText("\n[process exited with code " + code + "]\n");
        }
        function onErrorOccurred(message) {
            root.appendText("\n[terminal: " + message + "]\n");
        }
        function onRunningChanged(running) {
            root.sessionRunning = running;
            if (running) Qt.callLater(root.syncSize);
        }
    }

    Component.onCompleted: {
        if (root.session) {
            root.session.start(root.startDirectory, 80, 24, {});
        } else {
            root.appendText("Terminal backend unavailable: expose ModelBench::TerminalSession " +
                            "as the context property 'terminalSession'.\n");
        }
    }

    onWidthChanged: Qt.callLater(root.syncSize)
    onHeightChanged: Qt.callLater(root.syncSize)

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.sp1

        // ---- Toolbar ----
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            radius: Theme.radiusMd
            color: Theme.panel
            border.color: Theme.border
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.sp2
                spacing: Theme.sp2

                Rectangle {
                    Layout.preferredWidth: 8
                    Layout.preferredHeight: 8
                    radius: 4
                    color: root.sessionRunning ? Theme.success : Theme.textSubtle
                }
                Text {
                    text: root.sessionRunning ? "shell running" : "shell stopped"
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeSm
                }
                Text {
                    text: root.session ? root.session.workingDirectory : ""
                    color: Theme.textSubtle
                    font.pixelSize: Theme.fontSizeSm
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }
                Button {
                    text: "Restart"
                    onClicked: {
                        if (root.session)
                            root.session.start(root.startDirectory,
                                               root.session.columns,
                                               root.session.rows, {});
                    }
                }
                Button {
                    text: "Copy"
                    onClicked: root.copySelection()
                }
                Button {
                    text: "Clear"
                    onClicked: root.clear()
                }
            }
        }

        // ---- Output ----
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radiusMd
            color: Theme.bg
            border.color: Theme.border
            border.width: 1
            clip: true

            Flickable {
                id: outputView
                anchors.fill: parent
                anchors.margins: Theme.sp2
                contentWidth: width
                contentHeight: outputText.height
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                Text {
                    id: outputText
                    width: outputView.width
                    text: root.html
                    textFormat: Text.RichText
                    wrapMode: Text.NoWrap
                    color: Theme.text
                    selectByMouse: true
                    font.family: Theme.fontMono
                    font.pixelSize: root.fontSize
                }
            }
            MouseArea {
                anchors.fill: parent
                z: -1 // let Text handle selection; used only to refocus input
                onClicked: input.forceActiveFocus()
            }
        }

        // ---- Input line ----
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            radius: Theme.radiusMd
            color: Theme.panelAlt
            border.color: Theme.border
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.sp2
                spacing: Theme.sp1

                Text {
                    text: "❯"
                    color: Theme.accent
                    font.family: Theme.fontMono
                    font.pixelSize: root.fontSize
                }
                TextInput {
                    id: input
                    Layout.fillWidth: true
                    color: Theme.text
                    font.family: Theme.fontMono
                    font.pixelSize: root.fontSize
                    focus: true
                    selectByMouse: true

                    onAccepted: {
                        var line = input.text;
                        if (line.trim().length > 0) {
                            root._history.push(line);
                            root._historyIndex = root._history.length;
                        }
                        if (root.session) root.session.sendText(line + "\n");
                        input.text = "";
                    }

                    Keys.onPressed: function(event) {
                        if (event.key === Qt.Key_Up && root._history.length > 0) {
                            root._historyIndex = Math.max(0, root._historyIndex - 1);
                            input.text = root._history[root._historyIndex];
                            event.accepted = true;
                        } else if (event.key === Qt.Key_Down && root._history.length > 0) {
                            root._historyIndex = Math.min(root._history.length,
                                                          root._historyIndex + 1);
                            input.text = root._historyIndex < root._history.length
                                         ? root._history[root._historyIndex] : "";
                            event.accepted = true;
                        } else if (event.key === Qt.Key_L &&
                                   (event.modifiers & Qt.ControlModifier)) {
                            root.clear();
                            event.accepted = true;
                        } else if (event.key === Qt.Key_C &&
                                   (event.modifiers & Qt.ControlModifier)) {
                            if (root.session) root.session.interrupt();
                            root.appendText("^C\n");
                            event.accepted = true;
                        }
                    }
                }
            }
        }
    }
}
