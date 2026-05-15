pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import SiriusScope 1.0 as Sirius

Item {
    id: root

    property real viewMinHz: Sirius.FrequencyViewportModel.viewMinHz
    property real viewMaxHz: Sirius.FrequencyViewportModel.viewMaxHz
    property real globalMinHz: Sirius.FrequencyViewportModel.globalMinHz
    property real globalMaxHz: Sirius.FrequencyViewportModel.globalMaxHz
    property var ringBuffer: Sirius.WaterfallController.ringBuffer
    property bool retuning: true
    property string currentUtcText: Sirius.WaterfallController.currentUtcText
    property var timeTicks: []
    property var timeTicksVersion: Sirius.WaterfallController.timeTicksVersion

    function spanHz() {
        return Math.max(1.0, root.viewMaxHz - root.viewMinHz)
    }

    function xForHz(hz) {
        return (hz - root.viewMinHz) / root.spanHz() * plotArea.width
    }

    onViewMinHzChanged: waterfallGrid.requestPaint()
    onViewMaxHzChanged: waterfallGrid.requestPaint()
    onTimeTicksChanged: waterfallGrid.requestPaint()

    function refreshTimeTicks() {
        if (plotArea.height <= 0) {
            timeTicks = []
            return
        }
        root.timeTicks = Sirius.WaterfallController.visibleTimeTicks(plotArea.height)
    }

    ColumnLayout {
        anchors.fill: root
        spacing: 8

        Rectangle {
            id: plotFrame
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 220
            radius: Sirius.Theme.radiusInset
            color: Sirius.Theme.waterfallBackground
            border.color: Sirius.Theme.panelBorderSoft
            clip: true

            readonly property int timeScaleWidth: Sirius.Theme.leftAxisWidth

            Rectangle {
                id: timeScale
                anchors.left: plotFrame.left
                anchors.top: plotFrame.top
                anchors.bottom: plotFrame.bottom
                width: plotFrame.timeScaleWidth
                color: "#0A0F15"
                border.color: Sirius.Theme.panelBorderSoft

                Repeater {
                    model: root.timeTicks

                    Text {
                        id: timeTickLabel

                        required property var modelData

                        x: 5
                        y: Math.max(4, Math.min(timeScale.height - timeTickLabel.height - 4,
                                                timeTickLabel.modelData.y - timeTickLabel.height / 2))
                        width: timeScale.width - 8
                        text: timeTickLabel.modelData.label
                        color: timeTickLabel.modelData.major ? Sirius.Theme.textSecondary : Sirius.Theme.textMuted
                        font.family: Sirius.Theme.monoFontFamily
                        font.pixelSize: timeTickLabel.modelData.major ? 9 : 8
                        elide: Text.ElideRight
                        z: 5
                    }
                }
            }

            Item {
                id: plotArea
                anchors.left: timeScale.right
                anchors.right: plotFrame.right
                anchors.top: plotFrame.top
                anchors.bottom: plotFrame.bottom
                clip: true

                Rectangle {
                    anchors.fill: plotArea
                    color: Sirius.Theme.waterfallBackground
                    z: 0
                }

                Canvas {
                    id: waterfallGrid
                    anchors.fill: plotArea
                    z: 1
                    opacity: 0.9

                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)

                        ctx.lineWidth = 1
                        var frequencyTicks = Sirius.FrequencyGridModel.buildTicks(root.viewMinHz,
                                                                                  root.viewMaxHz,
                                                                                  Math.floor(width))
                        for (var i = 0; i < frequencyTicks.length; i++) {
                            var tick = frequencyTicks[i]
                            var x = root.xForHz(tick.frequencyHz)
                            ctx.strokeStyle = tick.major
                                ? String(Sirius.Theme.gridMajor)
                                : String(Sirius.Theme.gridSoft)
                            ctx.beginPath()
                            ctx.moveTo(x, 0)
                            ctx.lineTo(x, height)
                            ctx.stroke()
                        }

                        for (var j = 0; j < root.timeTicks.length; j++) {
                            var timeTick = root.timeTicks[j]
                            var y = timeTick.y
                            ctx.strokeStyle = timeTick.major
                                ? String(Sirius.Theme.gridMajor)
                                : String(Sirius.Theme.gridSoft)
                            ctx.beginPath()
                            ctx.moveTo(0, y)
                            ctx.lineTo(width, y)
                            ctx.stroke()
                        }
                    }
                }

                Sirius.WaterfallItem {
                    id: waterfall
                    anchors.fill: plotArea
                    ringBuffer: root.ringBuffer
                    directionalEnabled: Sirius.WaterfallController.directionalEnabled
                    colorGamma: Sirius.WaterfallController.colorGamma
                    directionDeadZone: Sirius.WaterfallController.directionDeadZone
                    directionalAlpha: Sirius.WaterfallController.directionalAlpha
                    z: 3
                }

                WheelHandler {
                    id: historyWheelHandler
                    target: plotArea
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad

                    onWheel: (event) => {
                        const steps = -event.angleDelta.y / 120
                        if (steps !== 0) {
                            Sirius.WaterfallController.scrollHistory(steps)
                        }
                        event.accepted = true
                    }
                }

                Repeater {
                    model: Sirius.BandModel
                    delegate: Item {
                        id: bandOverlayDelegate

                        required property real centerHz
                        required property real widthHz
                        required property color color

                        readonly property real halfWidth: bandOverlayDelegate.widthHz * 0.5
                        readonly property real bandMinHz: bandOverlayDelegate.centerHz - bandOverlayDelegate.halfWidth
                        readonly property real bandMaxHz: bandOverlayDelegate.centerHz + bandOverlayDelegate.halfWidth
                        readonly property real clampedMinHz: Math.max(bandMinHz, root.viewMinHz)
                        readonly property real clampedMaxHz: Math.min(bandMaxHz, root.viewMaxHz)
                        readonly property real bandWidth: Math.max(0, clampedMaxHz - clampedMinHz)

                        visible: bandWidth > 0
                        x: root.xForHz(clampedMinHz)
                        y: 0
                        width: Math.max(0, root.xForHz(clampedMaxHz) - root.xForHz(clampedMinHz))
                        height: plotArea.height
                        opacity: 0.10
                        z: 2

                        Rectangle {
                            anchors.fill: bandOverlayDelegate
                            color: bandOverlayDelegate.color
                        }
                    }
                }

                Rectangle {
                    id: retuningOverlay
                    anchors.fill: plotArea
                    color: "#0B0E13"
                    opacity: 0.32
                    visible: root.retuning
                    z: 8

                    Rectangle {
                        id: retuningLabel
                        anchors.centerIn: retuningOverlay
                        width: Math.min(retuningOverlay.width - 28, 150)
                        height: 34
                        radius: Sirius.Theme.radiusInset
                        color: Sirius.Theme.chipBackground
                        border.color: Sirius.Theme.panelBorder

                        Text {
                            anchors.centerIn: retuningLabel
                            text: "RETUNING"
                            color: Sirius.Theme.textPrimary
                            font.family: Sirius.Theme.monoFontFamily
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }
                    }
                }
            }
        }

        Rectangle {
            id: directionControls
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            Layout.minimumHeight: 28
            Layout.maximumHeight: 34
            radius: Sirius.Theme.radiusInset
            color: Sirius.Theme.insetBackground
            border.color: Sirius.Theme.panelBorderSoft

            RowLayout {
                anchors.fill: directionControls
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 12

                Text {
                    text: Sirius.WaterfallController.directionalEnabled ? "DIR ON" : "DIR OFF"
                    color: Sirius.WaterfallController.directionalEnabled ? Sirius.Theme.statusGood : Sirius.Theme.textMuted
                    font.family: Sirius.Theme.monoFontFamily
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    Layout.alignment: Qt.AlignVCenter
                }

                Text {
                    text: "\u03B3 " + Sirius.WaterfallController.colorGamma.toFixed(1)
                    color: Sirius.Theme.textSecondary
                    font.family: Sirius.Theme.monoFontFamily
                    font.pixelSize: 10
                    Layout.alignment: Qt.AlignVCenter
                }

                Text {
                    text: "D " + Sirius.WaterfallController.directionDeadZone.toFixed(2)
                    color: Sirius.Theme.textSecondary
                    font.family: Sirius.Theme.monoFontFamily
                    font.pixelSize: 10
                    Layout.alignment: Qt.AlignVCenter
                }

                Item {
                    Layout.fillWidth: true
                }

                Row {
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 6
                    Rectangle { width: 24; height: 8; radius: 2; color: Sirius.Theme.waterfallLeftHigh }
                    Text { text: qsTr("левый"); color: Sirius.Theme.textMuted; font.pixelSize: 10 }
                    Rectangle { width: 24; height: 8; radius: 2; color: Sirius.Theme.waterfallNeutralHigh }
                    Text { text: qsTr("равно"); color: Sirius.Theme.textMuted; font.pixelSize: 10 }
                    Rectangle { width: 24; height: 8; radius: 2; color: Sirius.Theme.waterfallRightHigh }
                    Text { text: qsTr("правый"); color: Sirius.Theme.textMuted; font.pixelSize: 10 }
                }

                Text {
                    text: root.currentUtcText
                    color: Sirius.Theme.textSecondary
                    font.family: Sirius.Theme.monoFontFamily
                    font.pixelSize: 10
                    Layout.alignment: Qt.AlignVCenter
                }
            }
        }
    }

    Connections {
        target: Sirius.FrequencyViewportModel
        function onViewportChanged() {
            root.retuning = true
            waterfallGrid.requestPaint()
        }
    }

    Connections {
        target: waterfall
        function onFreshDataChanged() {
            if (waterfall.freshData) {
                root.retuning = false
            }
        }
        function onActiveGenerationIdChanged() {
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
            root.refreshTimeTicks()
            waterfallGrid.requestPaint()
        }
    }

    Connections {
        target: Sirius.WaterfallController
        function onTimeTicksChanged() {
            root.refreshTimeTicks()
            waterfallGrid.requestPaint()
        }
        function onCurrentUtcTextChanged() {
            root.currentUtcText = Sirius.WaterfallController.currentUtcText
        }
    }

    Component.onCompleted: {
        root.retuning = !waterfall.freshData
        root.refreshTimeTicks()
        waterfallGrid.requestPaint()
    }
}
