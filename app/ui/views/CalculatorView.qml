import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    anchors.fill: parent

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            anchors.margins: Theme.sp5
            spacing: Theme.sp5

            Text { text: "Model Fit Calculator"; color: Theme.text; font.pixelSize: Theme.fontSizeXl; font.bold: true }
            Text { text: "Predict which models can run on your hardware. Estimates are predictions, not measurements."; color: Theme.textMuted; font.pixelSize: Theme.fontSizeSm; wrapMode: Text.Wrap; Layout.fillWidth: true }

            GroupBox {
                title: "Your Hardware"
                Layout.fillWidth: true
                GridLayout {
                    columns: 2
                    columnSpacing: Theme.sp4
                    rowSpacing: Theme.sp2
                    Text { text: "CPU cores"; color: Theme.textMuted }
                    TextField { text: "8" }
                    Text { text: "CPU threads"; color: Theme.textMuted }
                    TextField { text: "16" }
                    Text { text: "RAM (GB)"; color: Theme.textMuted }
                    TextField { text: "16" }
                    Text { text: "VRAM (GB)"; color: Theme.textMuted }
                    TextField { text: "8" }
                    Text { text: "GPU vendor"; color: Theme.textMuted }
                    ComboBox { model: ["None", "NVIDIA", "AMD", "Intel", "Apple"] }
                }
            }

            GroupBox {
                title: "Model Spec"
                Layout.fillWidth: true
                GridLayout {
                    columns: 2
                    columnSpacing: Theme.sp4
                    rowSpacing: Theme.sp2
                    Text { text: "Parameters (B)"; color: Theme.textMuted }
                    TextField { text: "7" }
                    Text { text: "Quantization"; color: Theme.textMuted }
                    ComboBox { model: ["Q2_K", "Q3_K_M", "Q4_K_M", "Q5_K_M", "Q6_K", "Q8_0", "F16"] }
                    Text { text: "Context length"; color: Theme.textMuted }
                    TextField { text: "4096" }
                }
            }

            Button {
                text: "Calculate Fit"
                Layout.preferredWidth: 160
            }

            GroupBox {
                title: "Result"
                Layout.fillWidth: true
                ColumnLayout {
                    spacing: Theme.sp2
                    Text { text: "Fits in RAM: —"; color: Theme.text }
                    Text { text: "Estimated RAM: — GB"; color: Theme.textMuted }
                    Text { text: "Estimated VRAM: — GB"; color: Theme.textMuted }
                    Text { text: "Recommended backend: —"; color: Theme.textMuted }
                    Text { text: "Recommended GPU layers: —"; color: Theme.textMuted }
                    Text { text: "Predicted tok/s: — (prediction, not measurement)"; color: Theme.textMuted; font.italic: true }
                }
            }
        }
    }
}
