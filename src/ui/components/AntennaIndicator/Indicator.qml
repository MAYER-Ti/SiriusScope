import QtQuick
import QtQuick.Layouts
import SiriusScope 1.0

Item {
    id: indicator

    property var targetAzimuthsDeg: []
    property real azimuthDeg: 0
    property bool hasSelectedSector: false
    property real selectedLeftAngle: 0
    property real selectedRightAngle: 0

    property real beamWidthDeg: 60
    property real innerRadiusRatio: 0.3
    property real beamOpacity: 0.45
    property real smoothing: 0.1
    property int renderFps: 60
    property real minimumSectorDeg: 5
    property bool sectorEditingEnabled: true

    signal sectorSelected(real leftAngle, real rightAngle)
    signal sectorCleared()

    property real _latestAzimuthDeg: 0
    property real _renderAzimuthDeg: 0
    property real _tickMs: 0
    property bool _dragging: false
    property bool _dragMoved: false
    property real _dragStartX: 0
    property real _dragStartY: 0
    property real _dragStartAngle: 0
    property bool _previewHasSector: false
    property real _previewLeftAngle: 0
    property real _previewRightAngle: 0

    readonly property real _blindLeftDeg: 170
    readonly property real _blindRightDeg: 190
    readonly property bool _displayHasSector: _previewHasSector || hasSelectedSector
    readonly property real _displayLeftAngle: _previewHasSector ? _previewLeftAngle : selectedLeftAngle
    readonly property real _displayRightAngle: _previewHasSector ? _previewRightAngle : selectedRightAngle
    readonly property real _scanSectorInnerRadius: dial.rInner + 6
    readonly property real _scanSectorOuterRadius: Math.max(_scanSectorInnerRadius + 1, dial.rOuter - 4)
    readonly property real _beamOuterRadius: Math.max(dial.rInner + 1, dial.rOuter - 4)

    function _norm360(deg) {
        var a = deg % 360.0
        if (a < 0) {
            a += 360.0
        }
        return a
    }

    function _deltaDeg(fromDeg, toDeg) {
        var d = _norm360(toDeg) - _norm360(fromDeg)
        if (d > 180) {
            d -= 360
        } else if (d < -180) {
            d += 360
        }
        return d
    }

    function _updateRenderAzimuth() {
        var target = _norm360(_latestAzimuthDeg)
        var cur = _norm360(_renderAzimuthDeg)
        var d = _deltaDeg(cur, target)
        _renderAzimuthDeg = _norm360(cur + d * smoothing)
    }

    function isInBlindZone(deg) {
        var a = _norm360(deg)
        return a > _blindLeftDeg && a < _blindRightDeg
    }

    function clampToSafeAngle(deg) {
        var a = _norm360(deg)
        if (!isInBlindZone(a)) {
            return a
        }
        return a < 180 ? _blindLeftDeg : _blindRightDeg
    }

    function toSafeCoord(deg) {
        var a = clampToSafeAngle(deg)
        if (a >= _blindRightDeg) {
            return a - _blindRightDeg
        }
        return a + _blindLeftDeg
    }

    function fromSafeCoord(coord) {
        var c = Math.max(0, Math.min(340, coord))
        if (c <= 170) {
            return _norm360(_blindRightDeg + c)
        }
        return c - _blindLeftDeg
    }

    function makeSafeSector(angleA, angleB) {
        var coordA = toSafeCoord(angleA)
        var coordB = toSafeCoord(angleB)
        var minCoord = Math.min(coordA, coordB)
        var maxCoord = Math.max(coordA, coordB)
        return {
            leftAngle: fromSafeCoord(minCoord),
            rightAngle: fromSafeCoord(maxCoord),
            spanDeg: maxCoord - minCoord
        }
    }

    function angleFromPoint(x, y) {
        var dx = x - dial.width * 0.5
        var dy = y - dial.height * 0.5
        return _norm360(Math.atan2(dx, -dy) * 180.0 / Math.PI)
    }

    function resetTargets() {
        targetTracker.clear()
    }

    onAzimuthDegChanged: {
        _latestAzimuthDeg = _norm360(azimuthDeg)
        if (_tickMs === 0) {
            _renderAzimuthDeg = _latestAzimuthDeg
        }
    }

    TargetTracker {
        id: targetTracker
        maxTargets: 15
        matchThresholdDeg: 4.0
        ttlMs: 12000
        fadeMs: 8000
    }

    Timer {
        id: renderTimer
        interval: Math.max(16, Math.round(1000 / Math.max(1, indicator.renderFps)))
        running: true
        repeat: true
        onTriggered: {
            indicator._tickMs = Date.now()
            indicator._updateRenderAzimuth()
            targetTracker.nowMs = indicator._tickMs
            targetTracker.ingest(indicator.targetAzimuthsDeg)
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.color: "transparent"
        border.width: 1
        radius: 4
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 6

        Item {
            id: dialArea
            Layout.fillWidth: true
            Layout.fillHeight: true

            readonly property real dialSize: Math.min(width, height)

            Item {
                id: dial
                width: dialArea.dialSize
                height: dialArea.dialSize
                anchors.centerIn: parent

                readonly property real rOuter: width * 0.5
                readonly property real rInner: rOuter * indicator.innerRadiusRatio
                readonly property real beamHalf: Math.max(0.5, indicator.beamWidthDeg * 0.5)
                readonly property real tickOuterPad: dial.rOuter * 0.03
                readonly property real majorTickLen: dial.rOuter * 0.085
                readonly property real midTickLen: dial.rOuter * 0.055
                readonly property real minorTickLen: dial.rOuter * 0.032
                readonly property real labelRadius: dial.rOuter * 0.80
                readonly property real targetRadius: dial.rInner + (dial.rOuter - dial.rInner) * 0.55

                function _xAt(radius, deg) {
                    var rad = deg * Math.PI / 180.0
                    return dial.width / 2 + radius * Math.sin(rad)
                }

                function _yAt(radius, deg) {
                    var rad = deg * Math.PI / 180.0
                    return dial.height / 2 - radius * Math.cos(rad)
                }

                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#17222D" }
                        GradientStop { position: 1.0; color: Theme.waterfallBackground }
                    }
                    border.color: "#344353"
                    border.width: 1
                    radius: width / 2
                }

                ArcBand {
                    anchors.fill: parent
                    startDeg: indicator._displayLeftAngle
                    endDeg: indicator._displayRightAngle
                    innerRadius: indicator._scanSectorInnerRadius
                    outerRadius: indicator._scanSectorOuterRadius
                    fillColor: "#477D9E"
                    strokeColor: "#72B8DA"
                    strokeWidth: 1
                    clockwise: true
                    bandOpacity: indicator._displayHasSector ? 0.38 : 0
                }

                ArcBand {
                    anchors.fill: parent
                    startDeg: indicator._renderAzimuthDeg - dial.beamHalf
                    endDeg: indicator._renderAzimuthDeg
                    innerRadius: dial.rInner
                    outerRadius: indicator._beamOuterRadius
                    fillColor: Theme.statusBad
                    strokeColor: "transparent"
                    strokeWidth: 0
                    clockwise: true
                    bandOpacity: indicator.beamOpacity
                }

                ArcBand {
                    anchors.fill: parent
                    startDeg: indicator._renderAzimuthDeg
                    endDeg: indicator._renderAzimuthDeg + dial.beamHalf
                    innerRadius: dial.rInner
                    outerRadius: indicator._beamOuterRadius
                    fillColor: Theme.statusGood
                    strokeColor: "transparent"
                    strokeWidth: 0
                    clockwise: true
                    bandOpacity: indicator.beamOpacity
                }

                ArcBand {
                    anchors.fill: parent
                    startDeg: indicator._blindLeftDeg
                    endDeg: indicator._blindRightDeg
                    innerRadius: 0
                    outerRadius: dial.rOuter
                    fillColor: "#2B3138"
                    strokeColor: "#7B8792"
                    strokeWidth: 1
                    clockwise: true
                    bandOpacity: 0.58
                }

                Repeater {
                    model: 5

                    Rectangle {
                        readonly property real deg: 172 + index * 4
                        width: 1
                        height: dial.rOuter
                        radius: 0.5
                        color: "#9AA5AF"
                        opacity: 0.16
                        x: dial.width / 2 - width / 2
                        y: dial.height / 2 - height
                        transform: Rotation {
                            origin.x: width / 2
                            origin.y: height
                            angle: deg
                        }
                    }
                }

                Repeater {
                    model: [indicator._blindLeftDeg, indicator._blindRightDeg]

                    Rectangle {
                        width: 2
                        height: dial.rOuter
                        radius: 1
                        color: "#B0BAC4"
                        opacity: 0.72
                        x: dial.width / 2 - width / 2
                        y: dial.height / 2 - height
                        transform: Rotation {
                            origin.x: width / 2
                            origin.y: height
                            angle: modelData
                        }
                    }
                }

                Repeater {
                    model: 12

                    Rectangle {
                        width: 2
                        height: dial.majorTickLen
                        radius: 1
                        color: "#8EA1B4"
                        opacity: 0.95
                        x: (dial.width - width) / 2
                        y: dial.tickOuterPad
                        transform: Rotation {
                            origin.x: width / 2
                            origin.y: dial.rOuter - dial.tickOuterPad
                            angle: index * 30
                        }
                    }
                }

                Repeater {
                    model: 24

                    Rectangle {
                        readonly property int deg: index * 15
                        visible: (deg % 30) !== 0
                        width: 2
                        height: dial.midTickLen
                        radius: 1
                        color: "#5A6D7F"
                        opacity: 0.82
                        x: (dial.width - width) / 2
                        y: dial.tickOuterPad
                        transform: Rotation {
                            origin.x: width / 2
                            origin.y: dial.rOuter - dial.tickOuterPad
                            angle: deg
                        }
                    }
                }

                Repeater {
                    model: 72

                    Rectangle {
                        readonly property int deg: index * 5
                        visible: (deg % 15) !== 0
                        width: 1
                        height: dial.minorTickLen
                        radius: 0.5
                        color: "#5A6D7F"
                        opacity: 0.48
                        x: (dial.width - width) / 2
                        y: dial.tickOuterPad
                        transform: Rotation {
                            origin.x: width / 2
                            origin.y: dial.rOuter - dial.tickOuterPad
                            angle: deg
                        }
                    }
                }

                Repeater {
                    model: 12

                    Text {
                        readonly property int deg: index * 30
                        text: deg.toString()
                        color: Theme.textLabel
                        font.pixelSize: Theme.fontSmall
                        x: dial._xAt(dial.labelRadius, deg) - width / 2
                        y: dial._yAt(dial.labelRadius, deg) - height / 2
                    }
                }

                Item {
                    id: targetsLayer
                    anchors.fill: parent

                    Repeater {
                        model: targetTracker.tracks.length

                        Item {
                            readonly property var tr: targetTracker.tracks[index]
                            readonly property real a: targetTracker.alpha(tr)
                            readonly property bool freshest: index === 0

                            Rectangle {
                                id: marker
                                width: freshest ? 6 : 5
                                height: Math.max(22, dial.rOuter * 0.20)
                                radius: width / 2
                                x: (dial.width - width) / 2
                                y: dial.rOuter * 0.055
                                color: Theme.bandColor(index % 5)
                                opacity: Math.max(0.18, a)
                                transform: Rotation {
                                    origin.x: marker.width / 2
                                    origin.y: dial.rOuter - marker.y
                                    angle: tr.az
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    width: dial.width * 0.03
                    height: width
                    radius: width / 2
                    color: Theme.textSecondary
                    anchors.centerIn: parent
                    opacity: 0.85
                }

                Rectangle {
                    width: dial.rInner * 2
                    height: width
                    radius: width / 2
                    anchors.centerIn: parent
                    color: "transparent"
                    border.color: "#56687A"
                    border.width: 1
                    opacity: 0.82
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: indicator.sectorEditingEnabled
                    acceptedButtons: Qt.LeftButton

                    onPressed: function(mouse) {
                        var angle = indicator.angleFromPoint(mouse.x, mouse.y)
                        indicator._dragMoved = false
                        indicator._previewHasSector = false

                        if (indicator.isInBlindZone(angle)) {
                            indicator._dragging = false
                            return
                        }

                        indicator._dragging = true
                        indicator._dragStartX = mouse.x
                        indicator._dragStartY = mouse.y
                        indicator._dragStartAngle = angle
                    }

                    onPositionChanged: function(mouse) {
                        if (!indicator._dragging) {
                            return
                        }

                        var dx = mouse.x - indicator._dragStartX
                        var dy = mouse.y - indicator._dragStartY
                        if (Math.sqrt(dx * dx + dy * dy) >= 4) {
                            indicator._dragMoved = true
                        }

                        var angle = indicator.clampToSafeAngle(indicator.angleFromPoint(mouse.x, mouse.y))
                        var sector = indicator.makeSafeSector(indicator._dragStartAngle, angle)
                        if (sector.spanDeg >= indicator.minimumSectorDeg) {
                            indicator._previewLeftAngle = sector.leftAngle
                            indicator._previewRightAngle = sector.rightAngle
                            indicator._previewHasSector = true
                        } else {
                            indicator._previewHasSector = false
                        }
                    }

                    onReleased: function(mouse) {
                        if (!indicator._dragging) {
                            indicator._previewHasSector = false
                            return
                        }

                        if (!indicator._dragMoved) {
                            indicator._dragging = false
                            indicator._previewHasSector = false
                            indicator.sectorCleared()
                            return
                        }

                        var angle = indicator.clampToSafeAngle(indicator.angleFromPoint(mouse.x, mouse.y))
                        var sector = indicator.makeSafeSector(indicator._dragStartAngle, angle)
                        indicator._dragging = false
                        indicator._previewHasSector = false

                        if (sector.spanDeg >= indicator.minimumSectorDeg) {
                            indicator.sectorSelected(sector.leftAngle, sector.rightAngle)
                        }
                    }

                    onCanceled: {
                        indicator._dragging = false
                        indicator._previewHasSector = false
                    }
                }
            }
        }
    }
}
