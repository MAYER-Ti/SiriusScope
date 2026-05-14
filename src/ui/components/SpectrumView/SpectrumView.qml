import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import SiriusScope 1.0

Item {
    id: root
    focus: true

    readonly property string monoFontFamily: Theme.monoFontFamily

    readonly property real amplitudeMin: 0
    readonly property real amplitudeMax: 500
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
    property var bandSettingsWindows: ({})
    property var bandSettingsSnapshots: ({})

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

    function samplesAsAmplitude(samples, sampleMin, sampleMax) {
        if (sampleMin >= amplitudeMin && sampleMax <= amplitudeMax) {
            return samples
        }

        var sourceSpan = Math.max(1.0, sampleMax - sampleMin)
        var amplitudeSpan = Math.max(1.0, amplitudeMax - amplitudeMin)
        var amplitudes = []
        for (var i = 0; i < samples.length; i++) {
            amplitudes.push(amplitudeMin + ((samples[i] - sampleMin) / sourceSpan) * amplitudeSpan)
        }
        return amplitudes
    }

    function clampAmplitude(value) {
        return Math.max(amplitudeMin, Math.min(amplitudeMax, value))
    }

    function thresholdForHz(hz) {
        var threshold = -1e9
        for (var i = 0; i < BandModel.count; i++) {
            var band = BandModel.get(i)
            var half = band.widthHz * 0.5
            var minHz = band.centerHz - half
            var maxHz = band.centerHz + half
            if (hz >= minHz && hz <= maxHz) {
                threshold = Math.max(threshold, band.thresholdAmplitude)
            }
        }
        return threshold
    }

    function bandIndexForId(bandId) {
        for (var i = 0; i < BandModel.count; i++) {
            if (BandModel.get(i).bandId === bandId) {
                return i
            }
        }
        return -1
    }

    function openBandSettingsWindow(index) {
        if (index < 0 || index >= BandModel.count) {
            return
        }

        var band = BandModel.get(index)
        var key = String(band.bandId)
        var existingWindow = bandSettingsWindows[key]
        if (existingWindow) {
            existingWindow.raise()
            existingWindow.requestActivate()
            return
        }

        bandSettingsSnapshots[key] = {
            centerHz: band.centerHz,
            widthHz: band.widthHz,
            thresholdAmplitude: band.thresholdAmplitude,
            inputAttenuatorDb: band.inputAttenuatorDb,
            outputAttenuatorDb: band.outputAttenuatorDb
        }

        var window = bandSettingsDialogComponent.createObject(root, {
            bandId: band.bandId,
            centerHz: band.centerHz,
            widthHz: band.widthHz,
            thresholdAmplitude: band.thresholdAmplitude,
            inputAttenuatorDb: band.inputAttenuatorDb,
            outputAttenuatorDb: band.outputAttenuatorDb,
            globalMinHz: root.globalMinHz,
            globalMaxHz: root.globalMaxHz,
            minAmplitude: root.amplitudeMin,
            maxAmplitude: root.amplitudeMax
        })

        if (!window) {
            return
        }

        window.transientParent = root.Window.window
        bandSettingsWindows[key] = window
        BandModel.setProperty(index, "settingsWindowOpen", true)

        window.saveRequested.connect(function(bandId, centerHz, widthHz, thresholdAmplitude,
                                             inputAttenuatorDb, outputAttenuatorDb) {
            var bandIndex = bandIndexForId(bandId)
            if (bandIndex < 0) {
                return
            }
            BandModel.setProperty(bandIndex, "centerHz", centerHz)
            BandModel.setProperty(bandIndex, "widthHz", widthHz)
            BandModel.setProperty(bandIndex, "thresholdAmplitude", thresholdAmplitude)
            BandModel.setProperty(bandIndex, "inputAttenuatorDb", inputAttenuatorDb)
            BandModel.setProperty(bandIndex, "outputAttenuatorDb", outputAttenuatorDb)
            SpectrumController.applyBandSettings(bandId, centerHz, widthHz, thresholdAmplitude,
                                                 inputAttenuatorDb, outputAttenuatorDb)
            bandSettingsSnapshots[String(bandId)] = {
                centerHz: centerHz,
                widthHz: widthHz,
                thresholdAmplitude: thresholdAmplitude,
                inputAttenuatorDb: inputAttenuatorDb,
                outputAttenuatorDb: outputAttenuatorDb
            }
            plot.requestPaint()
        })

        window.thresholdPreviewChanged.connect(function(bandId, thresholdAmplitude) {
            var bandIndex = bandIndexForId(bandId)
            if (bandIndex < 0) {
                return
            }
            BandModel.setProperty(bandIndex, "thresholdAmplitude", thresholdAmplitude)
            plot.requestPaint()
        })

        window.canceled.connect(function(bandId) {
            var snapshotKey = String(bandId)
            var snapshot = bandSettingsSnapshots[snapshotKey]
            var bandIndex = bandIndexForId(bandId)
            if (!snapshot || bandIndex < 0) {
                return
            }
            BandModel.setProperty(bandIndex, "centerHz", snapshot.centerHz)
            BandModel.setProperty(bandIndex, "widthHz", snapshot.widthHz)
            BandModel.setProperty(bandIndex, "thresholdAmplitude", snapshot.thresholdAmplitude)
            BandModel.setProperty(bandIndex, "inputAttenuatorDb", snapshot.inputAttenuatorDb)
            BandModel.setProperty(bandIndex, "outputAttenuatorDb", snapshot.outputAttenuatorDb)
            plot.requestPaint()
        })

        window.windowClosed.connect(function(bandId) {
            var closedKey = String(bandId)
            var bandIndex = bandIndexForId(bandId)
            if (bandIndex >= 0) {
                BandModel.setProperty(bandIndex, "settingsWindowOpen", false)
            }
            delete bandSettingsWindows[closedKey]
            delete bandSettingsSnapshots[closedKey]
            window.destroy()
        })

        window.raise()
        window.requestActivate()
    }

    function updateBandPreviewFromDrag(bandId, centerHz, widthHz) {
        var index = bandIndexForId(bandId)
        if (index < 0) {
            return
        }
        BandModel.setProperty(index, "centerHz", centerHz)
        BandModel.setProperty(index, "widthHz", widthHz)
        var window = bandSettingsWindows[String(bandId)]
        if (window) {
            window.updateDraftFrequenciesHz(centerHz, widthHz)
        }
        plot.requestPaint()
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

    Connections {
        target: SpectrumController
        function onSpectrumReady(minHz, maxHz, samples, sampleMinValue, sampleMaxValue) {
            if (Math.abs(minHz - viewMinHz) > 1 || Math.abs(maxHz - viewMaxHz) > 1) {
                return
            }
            rawSamples = samplesAsAmplitude(samples, sampleMinValue, sampleMaxValue)
            decimateAndRepaint()
        }
    }

    onViewMinHzChanged: scheduleSpectrumRequest()
    onViewMaxHzChanged: scheduleSpectrumRequest()

    Component.onCompleted: {
        scheduleSpectrumRequest()
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.plotBackgroundBottom
        border.color: Theme.panelBorderSoft
        border.width: 1
        radius: Theme.radiusInset
        clip: true

        Item {
            id: plotArea
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            anchors.topMargin: 10
            anchors.bottomMargin: 8

            Canvas {
                id: plot
                anchors.fill: parent

                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)

                    var background = ctx.createLinearGradient(0, 0, 0, height)
                    background.addColorStop(0, String(Theme.plotBackgroundTop))
                    background.addColorStop(1, String(Theme.plotBackgroundBottom))
                    ctx.fillStyle = background
                    ctx.fillRect(0, 0, width, height)

                    var spanHz = Math.max(1.0, viewMaxHz - viewMinHz)
                    var amplitudeSpan = Math.max(1.0, amplitudeMax - amplitudeMin)

                    ctx.strokeStyle = String(Theme.gridSoft)
                    ctx.lineWidth = 1

                    var tickCountX = 5
                    var tickCountY = 5

                    for (var i = 0; i < tickCountX; i++) {
                        var x = (i / (tickCountX - 1)) * width
                        ctx.strokeStyle = i === 0 || i === tickCountX - 1 ? String(Theme.gridMajor) : String(Theme.gridSoft)
                        ctx.beginPath()
                        ctx.moveTo(x, 0)
                        ctx.lineTo(x, height)
                        ctx.stroke()
                    }

                    for (var j = 0; j < tickCountY; j++) {
                        var y = (j / (tickCountY - 1)) * height
                        ctx.strokeStyle = j === 0 || j === tickCountY - 1 ? String(Theme.gridMajor) : String(Theme.gridSoft)
                        ctx.beginPath()
                        ctx.moveTo(0, y)
                        ctx.lineTo(width, y)
                        ctx.stroke()
                    }

                    ctx.fillStyle = String(Theme.textMuted)
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
                        var amplitude = amplitudeMax - (ty / (tickCountY - 1)) * amplitudeSpan
                        var ly = (ty / (tickCountY - 1)) * height
                        ctx.fillText(amplitude.toFixed(0), 4, ly)
                    }

                    ctx.save()
                    ctx.setLineDash([5, 5])
                    ctx.globalAlpha = 0.55
                    ctx.strokeStyle = String(Theme.statusWarn)
                    ctx.lineWidth = 1
                    for (var b = 0; b < BandModel.count; b++) {
                        var thresholdBand = BandModel.get(b)
                        var bandMin = thresholdBand.centerHz - thresholdBand.widthHz * 0.5
                        var bandMax = thresholdBand.centerHz + thresholdBand.widthHz * 0.5
                        var clippedMin = Math.max(viewMinHz, bandMin)
                        var clippedMax = Math.min(viewMaxHz, bandMax)
                        if (clippedMax <= clippedMin) {
                            continue
                        }
                        var thresholdY = height - (thresholdBand.thresholdAmplitude - amplitudeMin) / amplitudeSpan * height
                        var thresholdX0 = (clippedMin - viewMinHz) / spanHz * width
                        var thresholdX1 = (clippedMax - viewMinHz) / spanHz * width
                        ctx.beginPath()
                        ctx.moveTo(thresholdX0, thresholdY)
                        ctx.lineTo(thresholdX1, thresholdY)
                        ctx.stroke()
                    }
                    ctx.restore()

                    if (decimatedMinMax.length < 2) {
                        return
                    }

                    ctx.strokeStyle = String(Theme.signalCyan)
                    ctx.lineWidth = 1.6

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

                        minVal = clampAmplitude(minVal)
                        maxVal = clampAmplitude(maxVal)

                        var yMin = height - (minVal - amplitudeMin) / amplitudeSpan * height
                        var yMax = height - (maxVal - amplitudeMin) / amplitudeSpan * height

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
                model: BandModel
                delegate: BandItem {
                    bandId: model.bandId
                    centerHz: model.centerHz
                    widthHz: model.widthHz
                    thresholdAmplitude: model.thresholdAmplitude
                    bandColor: model.color
                    bandBorderColor: model.borderColor
                    bandTextColor: model.textColor
                    viewMinHz: root.viewMinHz
                    viewMaxHz: root.viewMaxHz
                    globalMinHz: root.globalMinHz
                    globalMaxHz: root.globalMaxHz
                    settingsWindowOpen: model.settingsWindowOpen
                    panModifierActive: root.spacePressed
                    z: 2

                    onConfigureRequested: (requestedBandId) => {
                        openBandSettingsWindow(index)
                    }

                    onBandPreviewMoved: (movedBandId, nextCenter, nextWidth) => {
                        updateBandPreviewFromDrag(movedBandId, nextCenter, nextWidth)
                    }
                }
            }
        }
    }

    Component {
        id: bandSettingsDialogComponent
        BandSettingsDialog {}
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
