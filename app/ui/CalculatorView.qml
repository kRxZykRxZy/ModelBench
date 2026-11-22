import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property var hw: app.hardwareInfo()

    function calculate() {
        var params = parseFloat(paramsField.text) || 7
        var quant = quantCombo.currentText
        var ctx = parseInt(ctxField.text) || 4096
        var bpp = 0.5
        if (quant.indexOf("F16") >= 0) bpp = 2.0
        else if (quant.indexOf("Q8") >= 0) bpp = 1.0
        else if (quant.indexOf("Q6") >= 0) bpp = 0.8
        else if (quant.indexOf("Q5") >= 0) bpp = 0.65
        else if (quant.indexOf("Q4") >= 0) bpp = 0.5
        else if (quant.indexOf("Q3") >= 0) bpp = 0.4
        else if (quant.indexOf("Q2") >= 0) bpp = 0.3
        var weightGB = params * 1e9 * bpp / 1073741824
        var kvGB = ctx * 4096 * 2 / 1073741824
        var estGB = weightGB + kvGB
        var ramGB = (root.hw.ramBytes || 0) / 1073741824
        var vramGB = (root.hw.vramBytes || 0) / 1073741824
        var fits = estGB <= ramGB * 0.85
        resultText.text =
            "Estimated RAM: " + estGB.toFixed(1) + " GB\n" +
            "Available RAM: " + ramGB.toFixed(1) + " GB\n" +
            "Fits in RAM: " + (fits ? "YES" : "NO") + "\n" +
            "VRAM: " + vramGB.toFixed(1) + " GB " + (vramGB >= estGB ? "(full GPU offload possible)" : "(partial/CPU)") + "\n" +
            "Note: prediction, not a measured benchmark."
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        ColumnLayout {
            width: parent.width
            anchors.margins: Theme.sp5
            spacing: Theme.sp4

            Text { text: "Model Fit Calculator"; color: Theme.text; font.pixelSize: Theme.fontSizeXl; font.bold: true }
            Text { text: "Predict whether a model fits your hardware. Estimates are predictions, not measurements."; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm; wrapMode: Text.Wrap; Layout.fillWidth: true }

            GroupBox {
                title: "Your Hardware"
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    spacing: Theme.sp2
                    Text { text: "CPU: " + (root.hw.cpuModel || "Unknown"); color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm }
                    Text { text: "RAM: " + Math.round((root.hw.ramBytes || 0)/1073741824) + " GB  ·  VRAM: " + Math.round((root.hw.vramBytes || 0)/1073741824) + " GB"; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm }
                }
            }

            GroupBox {
                title: "Model Spec"
                Layout.fillWidth: true
                GridLayout {
                    columns: 2
                    columnSpacing: Theme.sp4
                    rowSpacing: Theme.sp2
                    Text { text: "Parameters (billions)"; color: Theme.textMuted }
                    TextField { id: paramsField; text: "7"; color: Theme.text }
                    Text { text: "Quantization"; color: Theme.textMuted }
                    ComboBox {
                        id: quantCombo
                        model: ["Q2_K", "Q3_K_M", "Q4_K_M", "Q5_K_M", "Q6_K", "Q8_0", "F16"]
                        currentIndex: 2
                    }
                    Text { text: "Context length"; color: Theme.textMuted }
                    TextField { id: ctxField; text: "4096"; color: Theme.text }
                }
            }

            Button { text: "Calculate Fit"; onClicked: root.calculate(); Layout.preferredWidth: 160 }

            GroupBox {
                title: "Result"
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.sp2
                    Text {
                        id: resultText
                        text: "Enter a model spec and press Calculate Fit."
                        color: Theme.text
                        font.pixelSize: Theme.fontSizeMd
                        lineHeight: 1.5
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }
            }
        }
    }
}
