import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property var searchResults: []
    property string selectedModelId: ""
    property string statusText: "Ready"

    Component.onCompleted: {
        // Optionally load installed models here
    }

    Connections {
        target: hfClient
        function onSearchFinished(models) {
            root.searchResults = models
            resultList.model = models
            root.statusText = models.length + " models found"
        }
        function onSearchFailed(err) { root.statusText = "Search failed: " + err }
        function onFilesFinished(repoId, files) { root.statusText = files.length + " files for " + repoId }
        function onFilesFailed(repoId, err) { root.statusText = "Files error: " + err }
    }

    Connections {
        target: downloader
        function onProgress(received, total) {
            if (total > 0) root.statusText = "Downloading: " + Math.round(received/total*100) + "%"
        }
        function onFinished(path) { root.statusText = "Download complete: " + path }
        function onFailed(err) { root.statusText = "Download failed: " + err }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.sp5
        spacing: Theme.sp4

        Text { text: "Model Library"; color: Theme.text; font.pixelSize: Theme.fontSizeXl; font.bold: true }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.sp2
            TextField {
                id: searchField
                Layout.fillWidth: true
                placeholderText: "Search GGUF models on Hugging Face..."
                font.pixelSize: Theme.fontSizeMd
                color: Theme.text
                Keys.onReturnPressed: doSearch()
            }
            Button { text: "Search"; onClicked: doSearch() }
            Button {
                text: "Trending"
                onClicked: { searchField.text = ""; doSearch() }
            }
        }

        function doSearch() {
            root.statusText = "Searching..."
            hfClient.searchModels(searchField.text.trim(), 20)
        }

        Text { text: root.statusText; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm }

        ListView {
            id: resultList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: []
            delegate: Rectangle {
                width: resultList.width
                implicitHeight: 76
                color: root.selectedModelId === modelData.id ? Theme.panelAlt : "transparent"
                border.color: Theme.border
                border.width: 1
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.sp3
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.sp1
                        Text {
                            text: modelData.id || ""
                            color: Theme.text
                            font.pixelSize: Theme.fontSizeMd
                            font.bold: true
                            elide: Text.ElideRight
                        }
                        Text {
                            text: (modelData.author || "") + "  ·  " + (modelData.pipeline || "") + "  ·  ↓ " + (modelData.downloads || 0)
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSizeXs
                        }
                    }
                    Button {
                        text: "Files"
                        onClicked: hfClient.fetchModelFiles(modelData.id)
                    }
                    Button {
                        text: "Load"
                        onClicked: {
                            var base = "https://huggingface.co/" + modelData.id + "/resolve/main/"
                            // Try a common GGUF filename; users can import local files too
                            root.statusText = "Use 'Download' to get a GGUF, then load the local file."
                        }
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    z: -1
                    onClicked: root.selectedModelId = modelData.id
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 90
            radius: Theme.radiusMd
            color: Theme.panel
            border.color: Theme.border
            border.width: 1
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.sp3
                spacing: Theme.sp2
                Text { text: "Load a local GGUF model"; color: Theme.text; font.bold: true; font.pixelSize: Theme.fontSizeMd }
                RowLayout {
                    Layout.fillWidth: true
                    TextField {
                        id: localPath
                        Layout.fillWidth: true
                        placeholderText: "C:\\path\\to\\model.gguf"
                        font.pixelSize: Theme.fontSizeSm
                        color: Theme.text
                    }
                    Button {
                        text: "Load Model"
                        onClicked: {
                            if (inference.loadModelFile(localPath.text)) {
                                root.statusText = "Model loaded successfully"
                            } else {
                                root.statusText = "Failed to load model. Enable llama.cpp build or check path."
                            }
                        }
                    }
                }
            }
        }
    }
}
