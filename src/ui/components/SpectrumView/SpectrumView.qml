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
    readonly property real amplitudeMax: 127
    readonly property real viewMinHz: FrequencyViewportModel.viewMinHz
    readonly property real viewMaxHz: FrequencyViewportModel.viewMaxHz
    readonly property real globalMinHz: FrequencyViewportModel.globalMinHz
    readonly property real globalMaxHz: FrequencyViewportModel.globalMaxHz
    readonly property bool bandEditingLocked: BandConfigController.editingLocked
    readonly property color envelopeStrokeColor: "#72B8DA"
    property real minSpanHz: 1050e6
    property real envelopeLineWidth: 2.2
    property real envelopeCurveTension: 0.65
    property bool envelopeVisualSmoothingEnabled: true
    property real envelopeVisualSmoothingStrength: 0.25
    property int envelopeMaxRenderPoints: 768
    readonly property real maxSpanHz: globalMaxHz - globalMinHz
    property var rawSamples: []
    property var pendingEnvelopeSamples: []
    property var decimatedMinMax: []
    property bool signalRepaintPending: false
    property bool spacePressed: false
    property var bandSettingsWindows: ({})
    property bool pendingBandPreviewValid: false
    property int pendingBandPreviewId: -1
    property real pendingBandPreviewCenterHz: 0
    property real pendingBandPreviewWidthHz: 0
    property int lastGeneratorPulseRejectedBandId: -1
    property string lastGeneratorPulseRejectionReason: ""

    Connections {
        target: BandConfigController

        function onGeneratorPulseSettingsRejected(bandId, reason) {
            root.lastGeneratorPulseRejectedBandId = bandId
            root.lastGeneratorPulseRejectionReason = reason
        }
    }

    function decimateAndRepaint() {
        if (signalCanvas.width <= 0) {
            return
        }
        if (rawSamples.length === 0) {
            decimatedMinMax = []
        } else {
            var targetWidth = Math.min(root.envelopeMaxRenderPoints, Math.floor(signalCanvas.width))
            decimatedMinMax = SpectrumDecimator.decimateMinMax(rawSamples, targetWidth)
        }
        signalCanvas.requestPaint()
    }

    function xForHz(hz, pixelWidth) {
        return (hz - viewMinHz) / Math.max(1.0, viewMaxHz - viewMinHz) * pixelWidth
    }

    function clampAmplitude(value) {
        return Math.max(amplitudeMin, Math.min(amplitudeMax, value))
    }

    function smoothEnvelopeAmplitude(previousValue, currentValue, nextValue) {
        if (!envelopeVisualSmoothingEnabled) {
            return currentValue
        }

        var neighborhoodAverage = previousValue * 0.25 + currentValue * 0.50 + nextValue * 0.25
        var strength = Math.max(0.0, Math.min(1.0, envelopeVisualSmoothingStrength))
        return currentValue * (1.0 - strength) + neighborhoodAverage * strength
    }

    function buildEnvelopePoints(pixelWidth, pixelHeight) {
        if (decimatedMinMax.length < 2) {
            return []
        }

        var amplitudeSpan = Math.max(1.0, amplitudeMax - amplitudeMin)
        var pointCount = Math.floor(decimatedMinMax.length / 2)
        var amplitudes = []

        for (var point = 0; point < pointCount; point++) {
            var idx = point * 2
            if (idx + 1 >= decimatedMinMax.length) {
                break
            }
            amplitudes.push(clampAmplitude(decimatedMinMax[idx + 1]))
        }

        var points = []
        for (var amplitudeIndex = 0; amplitudeIndex < amplitudes.length; amplitudeIndex++) {
            var currentValue = amplitudes[amplitudeIndex]
            var previousValue = amplitudeIndex > 0 ? amplitudes[amplitudeIndex - 1] : currentValue
            var nextValue = amplitudeIndex + 1 < amplitudes.length ? amplitudes[amplitudeIndex + 1] : currentValue
            var displayValue = clampAmplitude(smoothEnvelopeAmplitude(previousValue, currentValue, nextValue))
            var x = amplitudes.length <= 1 ? pixelWidth * 0.5 : (amplitudeIndex / (amplitudes.length - 1)) * pixelWidth
            var y = pixelHeight - (displayValue - amplitudeMin) / amplitudeSpan * pixelHeight
            points.push({ "x": x + 0.5, "y": y })
        }
        return points
    }

    function traceEnvelopeBezierPath(ctx, points) {
        if (points.length < 2) {
            return false
        }

        ctx.beginPath()
        ctx.moveTo(points[0].x, points[0].y)

        if (points.length === 2) {
            ctx.lineTo(points[1].x, points[1].y)
            return true
        }

        var tension = Math.max(0.0, envelopeCurveTension)
        for (var index = 0; index < points.length - 1; index++) {
            var p0 = index > 0 ? points[index - 1] : points[index]
            var p1 = points[index]
            var p2 = points[index + 1]
            var p3 = index + 2 < points.length ? points[index + 2] : p2
            var c1x = p1.x + (p2.x - p0.x) * tension / 6.0
            var c1y = p1.y + (p2.y - p0.y) * tension / 6.0
            var c2x = p2.x - (p3.x - p1.x) * tension / 6.0
            var c2y = p2.y - (p3.y - p1.y) * tension / 6.0
            var minY = Math.min(p1.y, p2.y)
            var maxY = Math.max(p1.y, p2.y)

            c1y = Math.max(minY, Math.min(maxY, c1y))
            c2y = Math.max(minY, Math.min(maxY, c2y))
            ctx.bezierCurveTo(c1x, c1y, c2x, c2y, p2.x, p2.y)
        }
        return true
    }

    function bandIndexForId(bandId) {
        return BandListModel.indexForBandId(bandId)
    }

    function openBandSettingsWindow(index) {
        if (index < 0 || index >= BandListModel.count()) {
            return
        }

        var band = BandListModel.get(index)
        var key = String(band.bandId)
        var existingWindow = bandSettingsWindows[key]
        if (existingWindow) {
            existingWindow.readOnly = root.bandEditingLocked
            existingWindow.updateGeneratorPulseSettingsDraft(band.generatorPulsePeriodUs,
                                                             band.generatorPulseWidthUs)
            existingWindow.raise()
            existingWindow.requestActivate()
            return
        }

        var window = bandSettingsDialogComponent.createObject(root, {
            bandId: band.bandId,
            centerHz: band.centerHz,
            widthHz: band.widthHz,
            thresholdAmplitude: band.thresholdAmplitude,
            inputAttenuatorDb: band.inputAttenuatorDb,
            outputAttenuatorDb: band.outputAttenuatorDb,
            polarization: band.polarization,
            generatorPulsePeriodUs: band.generatorPulsePeriodUs,
            generatorPulseWidthUs: band.generatorPulseWidthUs,
            globalMinHz: root.globalMinHz,
            globalMaxHz: root.globalMaxHz,
            minAmplitude: 0,
            maxAmplitude: root.amplitudeMax,
            readOnly: root.bandEditingLocked
        })

        if (!window) {
            return
        }

        window.transientParent = root.Window.window
        bandSettingsWindows[key] = window
        BandListModel.setSettingsWindowOpen(band.bandId, true)

        window.saveRequested.connect(function(bandId, centerHz, widthHz, thresholdAmplitude,
                                             inputAttenuatorDb, outputAttenuatorDb, polarization) {
            if (root.bandEditingLocked) {
                window.showControllerError(qsTr("Запись включена. Настройки доступны только для просмотра."))
                return
            }
            var accepted = BandConfigController.applyBandSettings(bandId, centerHz, widthHz,
                                                                  thresholdAmplitude,
                                                                  inputAttenuatorDb,
                                                                  outputAttenuatorDb,
                                                                  polarization)
            if (!accepted) {
                window.showControllerError(qsTr("Настройки диапазона отклонены."))
                return
            }
            window.markSettingsSaved()
            plot.requestPaint()
        })

        window.generatorPulseSettingsSaveRequested.connect(function(bandId, pulsePeriodUs, pulseWidthUs) {
            if (root.bandEditingLocked) {
                window.showControllerError(qsTr("Запись включена. Настройки доступны только для просмотра."))
                return
            }

            root.lastGeneratorPulseRejectedBandId = -1
            root.lastGeneratorPulseRejectionReason = ""
            var accepted = BandConfigController.applyGeneratorPulseSettings(bandId,
                                                                            pulsePeriodUs,
                                                                            pulseWidthUs)
            if (!accepted) {
                var reason = root.lastGeneratorPulseRejectedBandId === bandId
                        ? root.lastGeneratorPulseRejectionReason
                        : ""
                window.showControllerError(reason.length > 0
                                           ? reason
                                           : qsTr("Параметры генератора отклонены."))
            }
        })

        window.thresholdPreviewChanged.connect(function(bandId, thresholdAmplitude) {
            if (root.bandEditingLocked) {
                return
            }
            if (BandConfigController.setBandThresholdPreview(bandId, thresholdAmplitude)) {
                plot.requestPaint()
            }
        })

        window.canceled.connect(function(bandId) {
            BandConfigController.cancelBandSettingsPreview(bandId)
            plot.requestPaint()
        })

        window.windowClosed.connect(function(bandId) {
            var closedKey = String(bandId)
            BandListModel.setSettingsWindowOpen(bandId, false)
            delete bandSettingsWindows[closedKey]
            window.destroy()
        })

        window.raise()
        window.requestActivate()
    }

    function updateBandSettingsDraftWindow(bandId, centerHz, widthHz) {
        var window = bandSettingsWindows[String(bandId)]
        if (window) {
            window.updateDraftFrequenciesHz(centerHz, widthHz)
        }
    }

    function updateBandSettingsWindowsReadOnly() {
        for (var key in bandSettingsWindows) {
            var window = bandSettingsWindows[key]
            if (window) {
                window.readOnly = root.bandEditingLocked
            }
        }
    }

    function queueBandPreviewFromDrag(bandId, centerHz, widthHz) {
        if (root.bandEditingLocked) {
            pendingBandPreviewValid = false
            bandPreviewTimer.stop()
            return
        }
        pendingBandPreviewValid = true
        pendingBandPreviewId = bandId
        pendingBandPreviewCenterHz = centerHz
        pendingBandPreviewWidthHz = widthHz
        updateBandSettingsDraftWindow(bandId, centerHz, widthHz)

        if (!bandPreviewTimer.running) {
            bandPreviewTimer.start()
        }
    }

    function flushBandPreviewFromDrag() {
        if (root.bandEditingLocked) {
            pendingBandPreviewValid = false
            return false
        }
        if (!pendingBandPreviewValid) {
            return true
        }

        var bandId = pendingBandPreviewId
        var centerHz = pendingBandPreviewCenterHz
        var widthHz = pendingBandPreviewWidthHz
        pendingBandPreviewValid = false

        if (!BandConfigController.previewBandSettings(bandId, centerHz, widthHz)) {
            return false
        }
        plot.requestPaint()
        return true
    }

    function finishBandPreviewFromDrag(bandId, centerHz, widthHz) {
        pendingBandPreviewValid = false
        bandPreviewTimer.stop()

        if (root.bandEditingLocked) {
            return
        }
        if (!BandConfigController.previewBandSettings(bandId, centerHz, widthHz)) {
            return
        }
        updateBandSettingsDraftWindow(bandId, centerHz, widthHz)
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
        id: bandPreviewTimer
        interval: 33
        repeat: false
        onTriggered: flushBandPreviewFromDrag()
    }

    Timer {
        id: signalRepaintTimer
        interval: ScanController.scanActive ? 100 : 66
        repeat: false
        onTriggered: {
            root.signalRepaintPending = false
            rawSamples = pendingEnvelopeSamples
            decimateAndRepaint()
        }
    }

    Connections {
        target: SpectrumEnvelopeController
        function onEnvelopeChanged(minHz, maxHz, samples) {
            if (Math.abs(minHz - viewMinHz) > 1 || Math.abs(maxHz - viewMaxHz) > 1) {
                return
            }
            pendingEnvelopeSamples = samples
            if (!signalRepaintPending) {
                signalRepaintPending = true
                signalRepaintTimer.start()
            }
        }
    }

    Connections {
        target: BandConfigController
        function onEditingLockedChanged() {
            if (BandConfigController.editingLocked) {
                pendingBandPreviewValid = false
                bandPreviewTimer.stop()
            }
            root.updateBandSettingsWindowsReadOnly()
            plot.requestPaint()
        }
    }

    onViewMinHzChanged: {
        rawSamples = []
        pendingEnvelopeSamples = []
        decimatedMinMax = []
        signalRepaintPending = false
        signalRepaintTimer.stop()
        plot.requestPaint()
        signalCanvas.requestPaint()
    }
    onViewMaxHzChanged: {
        rawSamples = []
        pendingEnvelopeSamples = []
        decimatedMinMax = []
        signalRepaintPending = false
        signalRepaintTimer.stop()
        plot.requestPaint()
        signalCanvas.requestPaint()
    }

    Component.onCompleted: {
        rawSamples = SpectrumEnvelopeController.envelopeSamples()
        decimateAndRepaint()
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.plotBackgroundBottom
        border.color: Theme.panelBorderSoft
        border.width: 1
        radius: Theme.radiusInset
        clip: true

        Item {
            id: spectrumLayout
            anchors.fill: parent
            anchors.topMargin: 10
            anchors.bottomMargin: 16

            Canvas {
                id: amplitudeAxis
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: Theme.leftAxisWidth

                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    ctx.fillStyle = String(Theme.plotBackgroundBottom)
                    ctx.fillRect(0, 0, width, height)

                    ctx.strokeStyle = String(Theme.panelBorderSoft)
                    ctx.lineWidth = 1
                    ctx.beginPath()
                    ctx.moveTo(width - 0.5, 0)
                    ctx.lineTo(width - 0.5, height)
                    ctx.stroke()

                    var amplitudeSpan = Math.max(1.0, amplitudeMax - amplitudeMin)
                    var tickCountY = 5

                    ctx.fillStyle = String(Theme.textMuted)
                    ctx.font = Theme.fontSmall + "px " + root.monoFontFamily
                    ctx.textAlign = "right"
                    ctx.textBaseline = "middle"

                    for (var ty = 0; ty < tickCountY; ty++) {
                        var amplitude = amplitudeMax - (ty / (tickCountY - 1)) * amplitudeSpan
                        var ly = (ty / (tickCountY - 1)) * height
                        ctx.fillText(amplitude.toFixed(0), width - 8, ly)
                    }
                }
            }

            Item {
                id: plotArea
                anchors.left: amplitudeAxis.right
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom

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

                        var tickCountY = 5
                        var frequencyTicks = FrequencyGridModel.buildTicks(viewMinHz,
                                                                           viewMaxHz,
                                                                           Math.floor(width))

                        for (var i = 0; i < frequencyTicks.length; i++) {
                            var tick = frequencyTicks[i]
                            var x = root.xForHz(tick.frequencyHz, width)
                            ctx.strokeStyle = tick.major ? String(Theme.gridMajor) : String(Theme.gridSoft)
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
                        ctx.font = Theme.fontSmall + "px " + root.monoFontFamily
                        ctx.textAlign = "center"
                        ctx.textBaseline = "bottom"

                        for (var t = 0; t < frequencyTicks.length; t++) {
                            var labelTick = frequencyTicks[t]
                            var lx = root.xForHz(labelTick.frequencyHz, width)
                            ctx.fillText(labelTick.label, lx, height - 2)
                        }

                        ctx.save()
                        ctx.setLineDash([5, 5])
                        ctx.globalAlpha = 0.55
                        ctx.strokeStyle = String(Theme.statusWarn)
                        ctx.lineWidth = 1
                        for (var b = 0; b < BandListModel.count(); b++) {
                            var thresholdBand = BandListModel.get(b)
                            var bandMin = thresholdBand.minHz
                            var bandMax = thresholdBand.maxHz
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
                    model: BandListModel
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
                        editingLocked: root.bandEditingLocked
                        z: 2

                        onConfigureRequested: (requestedBandId) => {
                            openBandSettingsWindow(index)
                        }

                        onBandPreviewMoved: (movedBandId, nextCenter, nextWidth) => {
                            queueBandPreviewFromDrag(movedBandId, nextCenter, nextWidth)
                        }

                        onBandPreviewFinished: (movedBandId, nextCenter, nextWidth) => {
                            finishBandPreviewFromDrag(movedBandId, nextCenter, nextWidth)
                        }
                    }
                }

                Canvas {
                    id: signalCanvas
                    anchors.fill: parent
                    enabled: false
                    z: 3

                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)

                        var points = buildEnvelopePoints(width, height)
                        if (points.length < 2) {
                            return
                        }

                        ctx.strokeStyle = String(root.envelopeStrokeColor)
                        ctx.lineWidth = root.envelopeLineWidth
                        ctx.lineJoin = "round"
                        ctx.lineCap = "round"
                        if (traceEnvelopeBezierPath(ctx, points)) {
                            ctx.stroke()
                        }
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
        target: plotArea
        function onWidthChanged() {
            plot.requestPaint()
            decimateAndRepaint()
        }
        function onHeightChanged() {
            plot.requestPaint()
            signalCanvas.requestPaint()
            amplitudeAxis.requestPaint()
        }
    }
}
