import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SiriusScope 1.0

Item {
    id: root

    property real viewMinHz: FrequencyViewportModel.viewMinHz
    property real viewMaxHz: FrequencyViewportModel.viewMaxHz
    property real globalMinHz: FrequencyViewportModel.globalMinHz
    property real globalMaxHz: FrequencyViewportModel.globalMaxHz
    property var ringBuffer: WaterfallController.ringBuffer
    property bool retuning: true
    property bool directionalEnabled: true
    property real gamma: 0.7
    property real directionThreshold: 0.10
    property string currentUtcText: "UTC 18:24:13"

    function spanHz() {
        return Math.max(1.0, viewMaxHz - viewMinHz)
    }

    function xForHz(hz) {
        return (hz - viewMinHz) / spanHz() * plotArea.width
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        Rectangle {
            id: plotFrame
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 220
            radius: Theme.radiusInset
            color: Theme.waterfallBackground
            border.color: Theme.panelBorderSoft
            clip: true

            readonly property int timeGutterWidth: Math.max(38, Math.min(46, width * 0.038))

            Rectangle {
                id: timeGutter
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: plotFrame.timeGutterWidth
                color: "#0A0F15"
                border.color: Theme.panelBorderSoft
            }

            Item {
                id: plotArea
                anchors.left: timeGutter.right
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                clip: true

                Rectangle {
                    anchors.fill: parent
                    color: Theme.waterfallBackground
                }

                WaterfallItem {
                    id: waterfall
                    anchors.fill: parent
                    ringBuffer: root.ringBuffer
                    z: 1
                }

                Canvas {
                    id: waterfallGrid
                    anchors.fill: parent
                    z: 2
                    opacity: 0.9

                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)

                        ctx.lineWidth = 1
                        for (var i = 1; i < 6; i++) {
                            var x = i / 6 * width
                            ctx.strokeStyle = String(Theme.gridMajor)
                            ctx.beginPath()
                            ctx.moveTo(x, 0)
                            ctx.lineTo(x, height)
                            ctx.stroke()
                        }

                        for (var j = 1; j < 6; j++) {
                            var y = j / 6 * height
                            ctx.strokeStyle = String(Theme.gridSoft)
                            ctx.beginPath()
                            ctx.moveTo(0, y)
                            ctx.lineTo(width, y)
                            ctx.stroke()
                        }
                    }
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

                        visible: bandWidth > 0
                        x: root.xForHz(clampedMinHz)
                        y: 0
                        width: Math.max(0, root.xForHz(clampedMaxHz) - root.xForHz(clampedMinHz))
                        height: plotArea.height
                        color: model.color
                        opacity: 0.10
                        z: 3
                    }
                }

                Rectangle {
                    id: retuningOverlay
                    anchors.fill: parent
                    color: "#0B0E13"
                    opacity: 0.32
                    visible: root.retuning
                    z: 8

                    Rectangle {
                        anchors.centerIn: parent
                        width: Math.min(parent.width - 28, 150)
                        height: 34
                        radius: Theme.radiusInset
                        color: Theme.chipBackground
                        border.color: Theme.panelBorder

                        Text {
                            anchors.centerIn: parent
                            text: "RETUNING"
                            color: Theme.textPrimary
                            font.family: Theme.monoFontFamily
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }
                    }
                }
            }

            Repeater {
                model: ["+00:00", "+00:05", "+00:10", "+00:15", "+00:20", "+00:25"]

                Text {
                    x: 5
                    y: Math.min(plotFrame.height - height - 4,
                                Math.max(4, index / 5 * plotFrame.height - height / 2))
                    width: timeGutter.width - 8
                    text: modelData
                    color: Theme.textMuted
                    font.family: Theme.monoFontFamily
                    font.pixelSize: 8
                    elide: Text.ElideRight
                    z: 5
                }
            }
        }

        Rectangle {
            id: directionControls
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            Layout.minimumHeight: 28
            Layout.maximumHeight: 34
            radius: Theme.radiusInset
            color: Theme.insetBackground
            border.color: Theme.panelBorderSoft

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 12

                Text {
                    text: root.directionalEnabled ? "DIR ON" : "DIR OFF"
                    color: root.directionalEnabled ? Theme.statusGood : Theme.textMuted
                    font.family: Theme.monoFontFamily
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    Layout.alignment: Qt.AlignVCenter
                }

                Text {
                    text: "\u03B3 " + root.gamma.toFixed(1)
                    color: Theme.textSecondary
                    font.family: Theme.monoFontFamily
                    font.pixelSize: 10
                    Layout.alignment: Qt.AlignVCenter
                }

                Text {
                    text: "D " + root.directionThreshold.toFixed(2)
                    color: Theme.textSecondary
                    font.family: Theme.monoFontFamily
                    font.pixelSize: 10
                    Layout.alignment: Qt.AlignVCenter
                }

                Item {
                    Layout.fillWidth: true
                }

                Row {
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 6
                    Rectangle { width: 24; height: 8; radius: 2; color: Theme.waterfallLeftHigh }
                    Text { text: qsTr("левый"); color: Theme.textMuted; font.pixelSize: 10 }
                    Rectangle { width: 24; height: 8; radius: 2; color: Theme.waterfallNeutralHigh }
                    Text { text: qsTr("равно"); color: Theme.textMuted; font.pixelSize: 10 }
                    Rectangle { width: 24; height: 8; radius: 2; color: Theme.waterfallRightHigh }
                    Text { text: qsTr("правый"); color: Theme.textMuted; font.pixelSize: 10 }
                }

                Text {
                    text: root.currentUtcText
                    color: Theme.textSecondary
                    font.family: Theme.monoFontFamily
                    font.pixelSize: 10
                    Layout.alignment: Qt.AlignVCenter
                }
            }
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

    Connections {
        target: plotArea
        function onWidthChanged() {
            waterfallGrid.requestPaint()
        }
        function onHeightChanged() {
            waterfallGrid.requestPaint()
        }
    }

    Component.onCompleted: {
        root.retuning = !waterfall.freshData
        waterfallGrid.requestPaint()
    }
}
