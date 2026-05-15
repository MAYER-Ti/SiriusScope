import QtQuick
import QtQuick.Shapes

Item {
    id: arcBand

    property real startDeg: 0
    property real endDeg: 0
    property real innerRadius: 0
    property real outerRadius: 0
    property color fillColor: "transparent"
    property color strokeColor: "transparent"
    property real strokeWidth: 0
    property bool clockwise: true
    property real bandOpacity: 1.0

    readonly property real centerX: width * 0.5
    readonly property real centerY: height * 0.5
    readonly property real effectiveInnerRadius: Math.max(0.001, innerRadius)
    readonly property real spanDeg: _spanDegrees(startDeg, endDeg, clockwise)

    visible: outerRadius > innerRadius && outerRadius > 0 && spanDeg > 0
    opacity: bandOpacity

    function _norm360(deg) {
        var a = deg % 360.0
        if (a < 0) {
            a += 360.0
        }
        return a
    }

    function _spanDegrees(fromDeg, toDeg, isClockwise) {
        var from = _norm360(fromDeg)
        var to = _norm360(toDeg)
        var span = isClockwise ? to - from : from - to
        if (span < 0) {
            span += 360.0
        }
        return span
    }

    function _xAt(radius, deg) {
        var rad = _norm360(deg) * Math.PI / 180.0
        return centerX + radius * Math.sin(rad)
    }

    function _yAt(radius, deg) {
        var rad = _norm360(deg) * Math.PI / 180.0
        return centerY - radius * Math.cos(rad)
    }

    Shape {
        anchors.fill: parent
        antialiasing: true

        ShapePath {
            fillColor: arcBand.fillColor
            strokeColor: arcBand.strokeColor
            strokeWidth: arcBand.strokeWidth

            PathMove {
                x: arcBand._xAt(arcBand.outerRadius, arcBand.startDeg)
                y: arcBand._yAt(arcBand.outerRadius, arcBand.startDeg)
            }
            PathArc {
                x: arcBand._xAt(arcBand.outerRadius, arcBand.endDeg)
                y: arcBand._yAt(arcBand.outerRadius, arcBand.endDeg)
                radiusX: arcBand.outerRadius
                radiusY: arcBand.outerRadius
                useLargeArc: arcBand.spanDeg > 180.0
                direction: arcBand.clockwise ? PathArc.Clockwise : PathArc.Counterclockwise
            }
            PathLine {
                x: arcBand._xAt(arcBand.effectiveInnerRadius, arcBand.endDeg)
                y: arcBand._yAt(arcBand.effectiveInnerRadius, arcBand.endDeg)
            }
            PathArc {
                x: arcBand._xAt(arcBand.effectiveInnerRadius, arcBand.startDeg)
                y: arcBand._yAt(arcBand.effectiveInnerRadius, arcBand.startDeg)
                radiusX: arcBand.effectiveInnerRadius
                radiusY: arcBand.effectiveInnerRadius
                useLargeArc: arcBand.spanDeg > 180.0
                direction: arcBand.clockwise ? PathArc.Counterclockwise : PathArc.Clockwise
            }
            PathLine {
                x: arcBand._xAt(arcBand.outerRadius, arcBand.startDeg)
                y: arcBand._yAt(arcBand.outerRadius, arcBand.startDeg)
            }
        }
    }
}
