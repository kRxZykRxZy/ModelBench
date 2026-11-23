import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property var searchResults: []
    property string selectedModelId: ""
    property string statusText: "Ready"
    property var modelFiles: []
    property var downloadedModels: ({})
    property string pendingDownloadModel: ""
    property var localModels: downloader.listDownloadedModels()

    function refreshLocalModels() {
        localModels = downloader.listDownloadedModels()
    }

    function doSearch() {
        root.statusText = "Searching..."
        hfClient.searchModels(searchField.text.trim(), 20)
    }

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
        function onFilesFinished(repoId, files) {
            root.modelFiles = files
            fileDialog.open()
        }
        function onFilesFailed(repoId, err) { root.statusText = "Files error: " + err }
    }

    Connections {
        target: downloader
        function onProgress(received, total) {
            if (total > 0) root.statusText = "Downloading: " + Math.round(received/total*100) + "%"
        }
        function onFinished(path) {
            root.statusText = "Download complete: " + path
            if (root.pendingDownloadModel) {
                var m = root.downloadedModels
                m[root.pendingDownloadModel] = path
                root.downloadedModels = m
                root.pendingDownloadModel = ""
            }
            root.refreshLocalModels()
        }
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
                        text: root.downloadedModels[modelData.id] ? "Load" : "Download"
                        onClicked: {
                            var localPath = root.downloadedModels[modelData.id]
                            if (localPath) {
                                if (inference.loadModelFile(localPath)) {
                                    root.statusText = "Loaded: " + localPath.split('/').pop()
                                } else {
                                    root.statusText = "Failed to load " + localPath
                                }
                            } else {
                                root.selectedModelId = modelData.id
                                hfClient.fetchModelFiles(modelData.id)
                            }
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

        // Downloaded models
        Text { text: "Downloaded Models"; color: Theme.text; font.bold: true; font.pixelSize: Theme.fontSizeLg }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 130
            radius: Theme.radiusMd
            color: Theme.panel
            border.color: Theme.border
            border.width: 1
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.sp2
                spacing: Theme.sp1
                Text { text: "Local GGUF files in " + downloader.modelsDirectory(); color: Theme.textMuted; font.pixelSize: Theme.fontSizeXs; wrapMode: Text.Wrap; Layout.fillWidth: true }
                ListView {
                    id: localList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: downloader.listDownloadedModels()
                    delegate: Rectangle {
                        width: localList.width
                        implicitHeight: 40
                        color: "transparent"
                        border.color: Theme.border
                        border.width: 1
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: Theme.sp2
                            Text {
                                Layout.fillWidth: true
                                text: modelData.split('/').pop()
                                color: Theme.text
                                font.pixelSize: Theme.fontSizeSm
                                elide: Text.ElideRight
                            }
                            Button {
                                text: "Load"
                                onClicked: {
                                    if (inference.loadModelFile(modelData)) {
                                        root.statusText = "Loaded " + modelData.split('/').pop()
                                    } else {
                                        root.statusText = "Failed to load " + modelData.split('/').pop() + " — rebuild with llama.cpp ON"
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // Load local file
        // Downloaded models
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 140
            radius: Theme.radiusMd
            color: Theme.panel
            border.color: Theme.border
            border.width: 1
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.sp3
                spacing: Theme.sp2
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "Downloaded Models"; color: Theme.text; font.bold: true; font.pixelSize: Theme.fontSizeMd }
                    Item { Layout.fillWidth: true }
                    Button { text: "Refresh"; onClicked: root.refreshLocalModels() }
                }
                Text { text: "Folder: " + downloader.modelsDirectory(); color: Theme.textSubtle; font.pixelSize: Theme.fontSizeXs; wrapMode: Text.Wrap; Layout.fillWidth: true }
                ListView {
                    id: localList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: root.localModels
                    delegate: Rectangle {
                        width: localList.width
                        implicitHeight: 40
                        color: "transparent"
                        border.color: Theme.border
                        border.width: 1
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: Theme.sp2
                            Text {
                                Layout.fillWidth: true
                                text: modelData.split('/').pop()
                                color: Theme.text
                                font.pixelSize: Theme.fontSizeSm
                                elide: Text.ElideRight
                            }
                            Button {
                                text: "Load"
                                onClicked: {
                                    if (inference.loadModelFile(modelData)) {
                                        root.statusText = "Loaded " + modelData.split('/').pop()
                                    } else {
                                        root.statusText = "Failed to load " + modelData.split('/').pop()
                                    }
                                }
                            }
                        }
                    }
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
                                root.statusText = "Failed to load. Rebuild with -DMODELFIT_ENABLE_LLAMA_CPP=ON"
                            }
                        }
                    }
                }
            }
        }
    }

    // File selection dialog for HF model downloads
    Dialog {
        id: fileDialog
        title: "Download GGUF from " + root.selectedModelId
        width: 560
        height: 400
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Close
        background: Rectangle { color: Theme.panel; border.color: Theme.border; border.width: 1 }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Theme.sp3
            spacing: Theme.sp2

            Text { text: "Select a GGUF file to download:"; color: Theme.text; font.bold: true; font.pixelSize: Theme.fontSizeMd }

            ListView {
                id: fileList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.modelFiles
                delegate: Rectangle {
                    width: fileList.width
                    implicitHeight: 44
                    color: "transparent"
                    border.color: Theme.border
                    border.width: 1
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.sp2
                        Text {
                            Layout.fillWidth: true
                            text: modelData.name || ""
                            color: Theme.text
                            font.pixelSize: Theme.fontSizeSm
                            elide: Text.ElideRight
                        }
                        Text {
                            text: modelData.size ? Math.round(modelData.size/1048576) + " MB" : ""
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSizeXs
                        }
                        Button {
                            text: "Download"
                            enabled: !downloader.isRunning()
                            onClicked: {
                                var fileName = modelData.name.split('/').pop()
                                var url = "https://huggingface.co/" + root.selectedModelId + "/resolve/main/" + modelData.name
                                var dest = downloader.modelsDirectory() + "/" + fileName
                                root.statusText = "Downloading " + fileName + "..."
                                root.pendingDownloadModel = root.selectedModelId
                                downloader.download(url, dest)
                                fileDialog.close()
                            }
                        }
                    }
                }
            }
        }
    }
}
