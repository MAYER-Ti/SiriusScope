import QtQuick
import QtQuick.Controls
import SiriusScope 1.0

Item {
    id: root

    property real viewMinHz: FrequencyViewportModel.viewMinHz
    property real viewMaxHz: FrequencyViewportModel.viewMaxHz
    property real globalMinHz: FrequencyViewportModel.globalMinHz
    property real globalMaxHz: FrequencyViewportModel.globalMaxHz
    property var ringBuffer: WaterfallController.ringBuffer
    property bool retuning: true

    function spanHz() {
        return Math.max(1.0, viewMaxHz - viewMinHz)
    }

    function xForHz(hz) {
        return (hz - viewMinHz) / spanHz() * width
    }

    Rectangle {
        anchors.fill: parent
        color: "#0d1016"
        border.color: "#2b2f36"
        border.width: 1
        radius: 6
    }

    WaterfallItem {
        id: waterfall
        anchors.fill: parent
        ringBuffer: root.ringBuffer
    }

    Repeater {
        model: BandModel
        delegate: Rectangle {
            readonly property real halfWidth: model.widthHz * 0.5
            readonly property real bandMinHz: model.centerHz - halfWidth
            readonly property real bandMaxHz: model.centerHz + halfWidth
            readonly property real clampedMinHz: Math.max(bandMinHz, root.viewMinHz)
            readonly property real clampedMaxHz: Math.min(bandMaxHz, root.viewMaxHz)
            readonly property real bandWidth: Math.max(0, clampedMaxHz - clampedMinHz)

            visible: model.enabled && bandWidth > 0
            x: root.xForHz(clampedMinHz)
            y: 0
            width: Math.max(0, root.xForHz(clampedMaxHz) - root.xForHz(clampedMinHz))
            height: root.height
            color: "#4bb4ff"
            opacity: 0.14
        }
    }

    Rectangle {
        id: retuningOverlay
        anchors.fill: parent
        color: "#0b0e13"
        opacity: 0.55
        visible: root.retuning

        Text {
            anchors.centerIn: parent
            text: "RETUNING"
            color: "#e1e6ee"
            font.pixelSize: 18
            font.bold: true
        }
    }

    Connections {
        target: FrequencyViewportModel
        function onViewportChanged() {
            root.retuning = true
        }
    }

    Connections {
        target: waterfall
        function onFreshDataChanged() {
            if (waterfall.freshData) {
                root.retuning = false
            }
        }
    }

    Component.onCompleted: {
        root.retuning = !waterfall.freshData
    }
}
