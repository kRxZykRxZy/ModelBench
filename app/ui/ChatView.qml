import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property string currentConvId: ""
    property string currentResponse: ""
    property var messages: []

    function loadConversations() {
        convList.model = conversations.getConversations()
    }

    function loadMessages() {
        if (currentConvId) {
            messages = conversations.getMessages(currentConvId)
            msgView.model = messages
        } else {
            messages = []
            msgView.model = []
        }
    }

    function newChat() {
        currentConvId = conversations.createConversation("New chat", "")
        currentResponse = ""
        loadConversations()
        loadMessages()
    }

    function sendMessage() {
        if (!textInput.text.trim()) return
        if (!currentConvId) newChat()
        var text = textInput.text.trim()
        textInput.text = ""
        conversations.addMessage(currentConvId, "user", text)
        currentResponse = ""
        loadMessages()
        if (inference.isLoaded()) {
            inference.generateText(text)
        } else {
            currentResponse = "[No model loaded. Load a model in the Models view to chat.]"
            finishResponse()
        }
    }

    function finishResponse() {
        if (currentResponse.trim().length > 0) {
            conversations.addMessage(currentConvId, "assistant", currentResponse)
        }
        currentResponse = ""
        loadMessages()
        loadConversations()
    }

    Component.onCompleted: {
        loadConversations()
        var convs = conversations.getConversations()
        if (convs.length > 0) { currentConvId = convs[0].id; loadMessages() }
    }

    Connections {
        target: inference
        function onTokenGenerated(token) { currentResponse += token }
        function onGenerationFinished(result) { finishResponse() }
        function onGenerationError(err) { currentResponse += "\n[Error: " + err + "]"; finishResponse() }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Conversation sidebar
        Rectangle {
            Layout.preferredWidth: 240
            Layout.fillHeight: true
            color: Theme.panel
            border.color: Theme.border
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.sp3
                spacing: Theme.sp2

                Button {
                    Layout.fillWidth: true
                    text: "+ New chat"
                    onClicked: root.newChat()
                }
                ListView {
                    id: convList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    delegate: Item {
                        width: convList.width
                        height: 40
                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.radiusSm
                            color: root.currentConvId === modelData.id ? Theme.panelAlt : "transparent"
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.sp2
                            text: modelData.title
                            color: root.currentConvId === modelData.id ? Theme.text : Theme.textMuted
                            font.pixelSize: Theme.fontSizeSm
                            elide: Text.ElideRight
                            width: parent.width - Theme.sp4
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                root.currentConvId = modelData.id
                                root.loadMessages()
                            }
                        }
                    }
                }
            }
        }

        // Main chat area
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Model status bar
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 36
                color: Theme.bgElevated
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.sp3
                    anchors.rightMargin: Theme.sp3
                    Text {
                        text: inference.isLoaded() ? "Model loaded · context " + inference.contextUsed() + "/" + inference.contextCapacity() : "No model loaded — load one in Models"
                        color: inference.isLoaded() ? Theme.success : Theme.warning
                        font.pixelSize: Theme.fontSizeSm
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        text: inference.isLoaded() ? "Stop" : "Load model"
                        onClicked: {
                            if (inference.isLoaded()) inference.cancelGeneration()
                            else window.currentView = "models"
                        }
                    }
                }
            }

            // Messages
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.bg
                ListView {
                    id: msgView
                    anchors.fill: parent
                    anchors.margins: Theme.sp3
                    clip: true
                    spacing: Theme.sp2
                    delegate: Rectangle {
                        width: msgView.width - Theme.sp4
                        radius: Theme.radiusMd
                        color: modelData.role === "user" ? Theme.panelAlt : Theme.panel
                        border.color: Theme.border
                        border.width: 1
                        implicitHeight: contentText.implicitHeight + Theme.sp4
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: Theme.sp2
                            Text {
                                text: modelData.role === "user" ? "You" : "ModelFit"
                                color: modelData.role === "user" ? Theme.accent : Theme.textMuted
                                font.pixelSize: Theme.fontSizeXs
                                font.bold: true
                            }
                            Text {
                                id: contentText
                                text: modelData.content
                                color: Theme.text
                                font.pixelSize: Theme.fontSizeMd
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }
                        }
                    }
                }
                // Streaming response overlay
                Rectangle {
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    anchors.margins: Theme.sp3
                    visible: root.currentResponse.length > 0
                    radius: Theme.radiusMd
                    color: Theme.panel
                    border.color: Theme.accent
                    border.width: 1
                    implicitHeight: streamText.implicitHeight + Theme.sp4
                    Text {
                        id: streamText
                        anchors.fill: parent
                        anchors.margins: Theme.sp2
                        text: "ModelFit: " + root.currentResponse + "|"
                        color: Theme.text
                        font.pixelSize: Theme.fontSizeMd
                        wrapMode: Text.Wrap
                    }
                }
            }

            // Composer
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 110
                color: Theme.bgElevated
                border.color: Theme.border
                border.width: 1
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.sp3
                    spacing: Theme.sp2
                    TextArea {
                        id: textInput
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        placeholderText: "Message ModelFit..."
                        wrapMode: TextArea.Wrap
                        font.pixelSize: Theme.fontSizeMd
                        color: Theme.text
                        background: Rectangle { color: "transparent" }
                        Keys.onReturnPressed: {
                            if (!(event.modifiers & Qt.ShiftModifier)) {
                                event.accepted = true
                                root.sendMessage()
                            }
                        }
                    }
                    Button {
                        text: "Send"
                        enabled: textInput.text.trim().length > 0
                        onClicked: root.sendMessage()
                    }
                }
            }
        }
    }
}
