pragma Singleton
import QtQuick

QtObject {
    id: theme

    // User preference: "system" | "dark" | "light"
    property string mode: "dark"

    readonly property bool dark: mode === "dark" || (mode === "system" && _systemDark)
    property bool _systemDark: true

    // Colors
    readonly property color bg: dark ? "#0F1115" : "#F6F7F9"
    readonly property color bgElevated: dark ? "#161922" : "#FFFFFF"
    readonly property color panel: dark ? "#1C2029" : "#FFFFFF"
    readonly property color panelAlt: dark ? "#232834" : "#F0F2F5"
    readonly property color border: dark ? "#2A2F3A" : "#D9DCE2"
    readonly property color borderStrong: dark ? "#3A4050" : "#BFC4CE"
    readonly property color text: dark ? "#E6E8EC" : "#1A1D23"
    readonly property color textMuted: dark ? "#9AA0AC" : "#5A606B"
    readonly property color textSubtle: dark ? "#6B7180" : "#8A909B"
    readonly property color accent: "#5B6CFF"
    readonly property color accentHover: "#6E7DFF"
    readonly property color accentPressed: "#4A5AE0"
    readonly property color success: "#3FB950"
    readonly property color warning: "#D29922"
    readonly property color danger: "#F85149"
    readonly property color info: "#58A6FF"

    // Spacing scale
    readonly property int sp1: 4
    readonly property int sp2: 8
    readonly property int sp3: 12
    readonly property int sp4: 16
    readonly property int sp5: 20
    readonly property int sp6: 24
    readonly property int sp8: 32

    // Radius
    readonly property int radiusSm: 6
    readonly property int radiusMd: 10
    readonly property int radiusLg: 14
    readonly property int radiusXl: 20

    // Typography
    readonly property string fontSans: "Inter, Segoe UI, SF Pro Text, sans-serif"
    readonly property string fontMono: "JetBrains Mono, Consolas, SF Mono, monospace"
    readonly property int fontSizeXs: 11
    readonly property int fontSizeSm: 12
    readonly property int fontSizeMd: 14
    readonly property int fontSizeLg: 16
    readonly property int fontSizeXl: 20
    readonly property int fontSize2xl: 24
}
