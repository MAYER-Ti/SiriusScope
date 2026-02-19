import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SiriusScope 1.0

Item {
    id: root
    focus: true

    readonly property string monoFontFamily: "Consolas, Monospace"

    property real minDb: -120
    property real maxDb: 0
    readonly property real viewMinHz: FrequencyViewportModel.viewMinHz
    readonly property real viewMaxHz: FrequencyViewportModel.viewMaxHz
    readonly property real globalMinHz: FrequencyViewportModel.globalMinHz
    readonly property real globalMaxHz: FrequencyViewportModel.globalMaxHz
    property real minSpanHz: 1050e6
    readonly property real maxSpanHz: globalMaxHz - globalMinHz
    property var rawSamples: []
    property var decimatedMinMax: []
    property real pendingRequestMinHz: viewMinHz
    property real pendingRequestMaxHz: viewMaxHz
    property bool spacePressed: false

    function scheduleSpectrumRequest() {
        pendingRequestMinHz = viewMinHz
        pendingRequestMaxHz = viewMaxHz
        spectrumRequestTimer.restart()
    }

    function decimateAndRepaint() {
        if (plot.width <= 0 || rawSamples.length === 0) {
            return
        }
        decimatedMinMax = SpectrumDecimator.decimateMinMax(rawSamples, Math.floor(plot.width))
        plot.requestPaint()
    }

    function thresholdForHz(hz) {
        var threshold = -1e9
        for (var i = 0; i < bandModel.count; i++) {
            var band = bandModel.get(i)
            if (!band.enabled) {
                continue
            }
            var half = band.widthHz * 0.5
            var minHz = band.centerHz - half
            var maxHz = band.centerHz + half
            if (hz >= minHz && hz <= maxHz) {
                threshold = Math.max(threshold, band.thresholdDb)
            }
        }
        return threshold
    }

    function formatHz(valueHz) {
        if (valueHz >= 1e9) {
            return (valueHz / 1e9).toFixed(2) + " GHz"
        }
        if (valueHz >= 1e6) {
            return (valueHz / 1e6).toFixed(0) + " MHz"
        }
        if (valueHz >= 1e3) {
            return (valueHz / 1e3).toFixed(0) + " kHz"
        }
        return valueHz.toFixed(0) + " Hz"
    }

    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Space) {
            spacePressed = true
            event.accepted = true
        }
    }

    Keys.onReleased: (event) => {
        if (event.key === Qt.Key_Space) {
            spacePressed = false
            event.accepted = true
        }
    }

    Timer {
        id: spectrumRequestTimer
        interval: 50
        repeat: false
        onTriggered: {
            SpectrumController.requestSpectrum(pendingRequestMinHz, pendingRequestMaxHz)
        }
    }

    ListModel {
        id: bandModel
        ListElement { bandId: 0; centerHz: 3.0e9; widthHz: 5.0e8; thresholdDb: -80; enabled: true }
        ListElement { bandId: 1; centerHz: 5.795e9; widthHz: 4.10e8; thresholdDb: -85; enabled: true }
        ListElement { bandId: 2; centerHz: 8.25e9; widthHz: 5.0e8; thresholdDb: -78; enabled: true }
        ListElement { bandId: 3; centerHz: 9.55e9; widthHz: 5.0e8; thresholdDb: -90; enabled: true }
        ListElement { bandId: 4; centerHz: 1.425e10; widthHz: 5.0e8; thresholdDb: -82; enabled: true }
    }

    Connections {
        target: SpectrumController
        function onSpectrumReady(minHz, maxHz, samples, minDbValue, maxDbValue) {
            if (Math.abs(minHz - viewMinHz) > 1 || Math.abs(maxHz - viewMaxHz) > 1) {
                return
            }
            rawSamples = samples
            minDb = minDbValue
            maxDb = maxDbValue
            decimateAndRepaint()
        }
    }

    onViewMinHzChanged: scheduleSpectrumRequest()
    onViewMaxHzChanged: scheduleSpectrumRequest()
    onMinDbChanged: plot.requestPaint()
    onMaxDbChanged: plot.requestPaint()

    Component.onCompleted: {
        scheduleSpectrumRequest()
    }

    Rectangle {
        anchors.fill: parent
        color: "#0f131a"
        border.color: "#2b2f36"
        border.width: 1
        radius: 6

        Item {
            id: plotArea
            anchors.fill: parent
            anchors.margins: 8

            Canvas {
                id: plot
                anchors.fill: parent

                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)

                    ctx.fillStyle = "#0f131a"
                    ctx.fillRect(0, 0, width, height)

                    var spanHz = Math.max(1.0, viewMaxHz - viewMinHz)
                    var dbSpan = Math.max(1.0, maxDb - minDb)

                    ctx.strokeStyle = "#222a33"
                    ctx.lineWidth = 1

                    var tickCountX = 5
                    var tickCountY = 5

                    for (var i = 0; i < tickCountX; i++) {
                        var x = (i / (tickCountX - 1)) * width
                        ctx.beginPath()
                        ctx.moveTo(x, 0)
                        ctx.lineTo(x, height)
                        ctx.stroke()
                    }

                    for (var j = 0; j < tickCountY; j++) {
                        var y = (j / (tickCountY - 1)) * height
                        ctx.beginPath()
                        ctx.moveTo(0, y)
                        ctx.lineTo(width, y)
                        ctx.stroke()
                    }

                    ctx.fillStyle = "#a5b0bd"
                    ctx.font = "10px " + root.monoFontFamily
                    ctx.textAlign = "center"
                    ctx.textBaseline = "bottom"

                    for (var t = 0; t < tickCountX; t++) {
                        var fx = viewMinHz + (t / (tickCountX - 1)) * spanHz
                        var label = formatHz(fx)
                        var lx = (t / (tickCountX - 1)) * width
                        ctx.fillText(label, lx, height - 2)
                    }

                    ctx.textAlign = "left"
                    ctx.textBaseline = "middle"
                    for (var ty = 0; ty < tickCountY; ty++) {
                        var db = maxDb - (ty / (tickCountY - 1)) * dbSpan
                        var ly = (ty / (tickCountY - 1)) * height
                        ctx.fillText(db.toFixed(0) + " dB", 4, ly)
                    }

                    if (decimatedMinMax.length < 2) {
                        return
                    }

                    ctx.strokeStyle = "#4ea1ff"
                    ctx.lineWidth = 1

                    for (var px = 0; px < width; px++) {
                        var idx = px * 2
                        if (idx + 1 >= decimatedMinMax.length) {
                            break
                        }
                        var minVal = decimatedMinMax[idx]
                        var maxVal = decimatedMinMax[idx + 1]
                        var freqHz = viewMinHz + (px / width) * spanHz
                        var bandThreshold = thresholdForHz(freqHz)

                        if (bandThreshold > -1e8) {
                            if (maxVal < bandThreshold) {
                                continue
                            }
                            if (minVal < bandThreshold) {
                                minVal = bandThreshold
                            }
                        }

                        var yMin = height - (minVal - minDb) / dbSpan * height
                        var yMax = height - (maxVal - minDb) / dbSpan * height

                        ctx.beginPath()
                        ctx.moveTo(px + 0.5, yMin)
                        ctx.lineTo(px + 0.5, yMax)
                        ctx.stroke()
                    }
                }
            }

            MouseArea {
                id: interactionArea
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                hoverEnabled: false
                preventStealing: true
                z: 0

                property bool panning: false
                property real panStartX: 0
                property real panStartMinHz: 0
                property real panStartMaxHz: 0

                onWheel: (wheel) => {
                    var spanHz = Math.max(1.0, viewMaxHz - viewMinHz)
                    var anchorHz = viewMinHz + (wheel.x / width) * spanHz
                    var zoomFactor = Math.pow(0.999, wheel.angleDelta.y)
                    var nextSpan = spanHz * zoomFactor
                    nextSpan = Math.max(minSpanHz, Math.min(maxSpanHz, nextSpan))

                    var anchorRatio = (anchorHz - viewMinHz) / spanHz
                    var nextMin = anchorHz - anchorRatio * nextSpan
                    var nextMax = nextMin + nextSpan

                    if (nextMin < globalMinHz) {
                        nextMin = globalMinHz
                        nextMax = nextMin + nextSpan
                    }
                    if (nextMax > globalMaxHz) {
                        nextMax = globalMaxHz
                        nextMin = nextMax - nextSpan
                    }

                    FrequencyViewportModel.setViewport(nextMin, nextMax, "SpectrumView")
                }

                onPressed: (mouse) => {
                    var usePan = mouse.button === Qt.MiddleButton
                        || (mouse.button === Qt.LeftButton && spacePressed)
                    if (!usePan) {
                        return
                    }
                    panning = true
                    panStartX = mouse.x
                    panStartMinHz = viewMinHz
                    panStartMaxHz = viewMaxHz
                }

                onPositionChanged: (mouse) => {
                    if (!panning) {
                        return
                    }
                    var spanHz = Math.max(1.0, panStartMaxHz - panStartMinHz)
                    var deltaHz = (mouse.x - panStartX) / width * spanHz
                    var nextMin = panStartMinHz - deltaHz
                    var nextMax = panStartMaxHz - deltaHz

                    if (nextMin < globalMinHz) {
                        nextMin = globalMinHz
                        nextMax = nextMin + spanHz
                    }
                    if (nextMax > globalMaxHz) {
                        nextMax = globalMaxHz
                        nextMin = nextMax - spanHz
                    }

                    FrequencyViewportModel.setViewport(nextMin, nextMax, "SpectrumView")
                }

                onReleased: (mouse) => {
                    panning = false
                }
            }

            Repeater {
                id: bandRepeater
                model: bandModel
                delegate: BandItem {
                    bandId: model.bandId
                    centerHz: model.centerHz
                    widthHz: model.widthHz
                    thresholdDb: model.thresholdDb
                    enabled: model.enabled
                    viewMinHz: root.viewMinHz
                    viewMaxHz: root.viewMaxHz
                    globalMinHz: root.globalMinHz
                    globalMaxHz: root.globalMaxHz
                    minDb: root.minDb
                    maxDb: root.maxDb
                    panModifierActive: root.spacePressed
                    z: 2

                    onBandEdited: (nextCenter, nextWidth, isFinal) => {
                        bandModel.setProperty(index, "centerHz", nextCenter)
                        bandModel.setProperty(index, "widthHz", nextWidth)
                        plot.requestPaint()
                    }

                    onThresholdEdited: (nextThreshold, isFinal) => {
                        bandModel.setProperty(index, "thresholdDb", nextThreshold)
                        plot.requestPaint()
                    }

                    onEnabledEdited: (nextEnabled, isFinal) => {
                        bandModel.setProperty(index, "enabled", nextEnabled)
                        plot.requestPaint()
                    }
                }
            }
        }
    }

    Connections {
        target: plot
        function onWidthChanged() {
            decimateAndRepaint()
        }
        function onHeightChanged() {
            plot.requestPaint()
        }
    }
}
