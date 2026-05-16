pragma Singleton

import QtQuick

QtObject {
    readonly property color appBackgroundDeep: "#070A0E"
    readonly property color appBackgroundTop: "#151C24"
    readonly property color appBackgroundBottom: "#0A0E13"
    readonly property color chromeTopBar: "#121922"

    readonly property color panelTop: "#16202A"
    readonly property color panelBottom: "#10161E"
    readonly property color panelBorder: "#2D3945"
    readonly property color panelBorderSoft: "#25313D"
    readonly property color insetBackground: "#0C1118"
    readonly property color chipBackground: "#17222D"

    readonly property color plotBackgroundTop: "#101720"
    readonly property color plotBackgroundBottom: "#090D13"
    readonly property color waterfallBackground: "#0B1016"
    readonly property color gridMajor: "#22303B"
    readonly property color gridSoft: "#1A2630"
    readonly property color divider: "#2D3945"

    readonly property color textPrimary: "#EEF4FB"
    readonly property color textSecondary: "#AFC0CE"
    readonly property color textLabel: "#9FAFBD"
    readonly property color textMuted: "#8393A3"
    readonly property color textVeryMuted: "#627182"

    readonly property color statusGood: "#35D07F"
    readonly property color statusWarn: "#E5B84B"
    readonly property color statusBad: "#F05D55"
    readonly property color signalCyan: "#4BB4FF"
    readonly property color signalMagenta: "#E46BD4"
    readonly property color signalViolet: "#8A7CFF"
    readonly property color signalOrange: "#FF9C55"

    readonly property color waterfallLeftLow: "#6F4146"
    readonly property color waterfallLeft: "#B96C70"
    readonly property color waterfallLeftHigh: "#DF9A9A"
    readonly property color waterfallNeutralLow: "#343D47"
    readonly property color waterfallNeutral: "#747E88"
    readonly property color waterfallNeutralHigh: "#B8C0C8"
    readonly property color waterfallRightLow: "#3B6549"
    readonly property color waterfallRight: "#6EBE80"
    readonly property color waterfallRightHigh: "#A3E9AE"

    readonly property int radiusPanel: 6
    readonly property int radiusInset: 4
    readonly property int pageMargin: 16
    readonly property int panelPadding: 18
    readonly property int compactPadding: 10
    readonly property int leftAxisWidth: 88
    readonly property int rowSpacing: 12
    readonly property string monoFontFamily: "JetBrains Mono, Consolas, monospace"

    readonly property int fontSmall: 14
    readonly property int fontNormal: 16
    readonly property int fontMedium: 18
    readonly property int fontLarge: 22
    readonly property int fontTitle: 24

    function bandColor(index) {
        if (index === 0) return "#4BB4FF"
        if (index === 1) return "#35D07F"
        if (index === 2) return "#E5B84B"
        if (index === 3) return "#E46BD4"
        if (index === 4) return "#8A7CFF"
        return "#AFC0CE"
    }

    function bandBorderColor(index) {
        if (index === 0) return "#7FCBFF"
        if (index === 1) return "#7CF3A9"
        if (index === 2) return "#FFD671"
        if (index === 3) return "#FF9AF0"
        if (index === 4) return "#B6AEFF"
        return "#C9D4DF"
    }

    function bandTextColor(index) {
        if (index === 0) return "#DDF4FF"
        if (index === 1) return "#E3FFF0"
        if (index === 2) return "#FFF2C5"
        if (index === 3) return "#FFE0FA"
        if (index === 4) return "#ECE9FF"
        return "#EEF4FB"
    }
}
