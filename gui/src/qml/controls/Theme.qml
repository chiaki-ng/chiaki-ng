pragma Singleton
import QtQuick

// Central design tokens for the chiaki-ng UI.
// Everything here is static: no bindings that tick during a stream.
QtObject {
    // Surfaces (darkest -> lightest)
    readonly property color bgDeep: "#07080D"
    readonly property color bg: "#0B0D14"
    readonly property color surface: "#131621"
    readonly property color surfaceRaised: "#1A1E2C"
    readonly property color surfaceHover: "#212638"
    readonly property color surfaceActive: "#2A3046"
    readonly property color scrim: "#B3050609"

    // Hairlines
    readonly property color stroke: "#14FFFFFF"
    readonly property color strokeStrong: "#26FFFFFF"

    // Text
    readonly property color text: "#F1F3F9"
    readonly property color textSecondary: "#A3AABD"
    readonly property color textTertiary: "#6B7288"

    // Brand / accents
    readonly property color accent: "#4D8DFF"
    readonly property color accentAlt: "#8B5CF6"
    readonly property color accentBright: "#7FB0FF"
    readonly property color accentSoft: "#2E4D8DFF"
    readonly property color accentGlow: "#664D8DFF"

    // Status
    readonly property color success: "#34D399"
    readonly property color successSoft: "#2634D399"
    readonly property color warning: "#FBBF24"
    readonly property color warningSoft: "#26FBBF24"
    readonly property color danger: "#FB7185"
    readonly property color dangerSoft: "#26FB7185"
    readonly property color info: "#38BDF8"
    readonly property color infoSoft: "#2638BDF8"

    // Shape
    readonly property int radiusS: 10
    readonly property int radiusM: 14
    readonly property int radiusL: 20
    readonly property int radiusXL: 28

    // Type
    readonly property string fontFamily: "Inter"
    readonly property int fontDisplay: 38
    readonly property int fontTitle: 28
    readonly property int fontHeadline: 23
    readonly property int fontBody: 18
    readonly property int fontLabel: 16
    readonly property int fontCaption: 14
    readonly property int fontMicro: 12

    // Motion
    readonly property int durFast: 120
    readonly property int durMed: 220
    readonly property int durSlow: 360

    function icon(name) {
        return "qrc:/ui/" + name + ".svg";
    }
}
