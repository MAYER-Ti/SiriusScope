import QtQuick
import QtQuick.Shapes
import QtQuick.Layouts
import QtQuick.Controls

Item {
    id: indicator

    // Вход: обновляется хоть каждые 10 мс (0..359.9)
    property real azimuthDeg: 0

    // Настройки "луча" антенны
    property real beamWidthDeg: 60          // полный угол сектора
    property real innerRadiusRatio: 0.5    // отверстие в центре (0..1)
    property real beamOpacity: 0.55         // базовая прозрачность
    property real smoothing: 0.35           // 0..1: скорость подтягивания renderAzimuth к latestAzimuth
    property int renderFps: 60              // частота обновления UI

    // Внутреннее состояние
    property real _latestAzimuthDeg: 0
    property real _renderAzimuthDeg: 0
    property real _tickMs: 0

    function _norm360(deg) {
        var a = deg % 360.0
        if (a < 0) a += 360.0
        return a
    }

    // signed delta in [-180, 180]
    function _deltaDeg(fromDeg, toDeg) {
        var d = _norm360(toDeg) - _norm360(fromDeg)
        if (d > 180) d -= 360
        else if (d < -180) d += 360
        return d
    }

    function _updateRenderAzimuth() {
        var target = _norm360(_latestAzimuthDeg)
        var cur = _norm360(_renderAzimuthDeg)

        var d = _deltaDeg(cur, target)
        cur = _norm360(cur + d * smoothing)

        _renderAzimuthDeg = cur
    }

    onAzimuthDegChanged: {
        _latestAzimuthDeg = _norm360(azimuthDeg)
        // первое значение ставим сразу, без "догонялок"
        if (_tickMs === 0) {
            _renderAzimuthDeg = _latestAzimuthDeg
        }
    }

    Timer {
        id: renderTimer
        interval: Math.max(16, Math.round(1000 / Math.max(1, indicator.renderFps)))
        running: true
        repeat: true
        onTriggered: {
            indicator._tickMs = Date.now()
            indicator._updateRenderAzimuth()
        }
    }

    // --- Background / Frame -------------------------------------------------
    Rectangle {
        id: frame
        anchors.fill: parent
        color: "#f3f4f6"
        border.color: "#b5bcc7"
        border.width: 1
        radius: 4
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 6

        // RowLayout {
        //     Layout.fillWidth: true
        //     spacing: 8

        //     Label {
        //         text: qsTr("Пеленгатор")
        //         font.bold: true
        //         color: "#2b2f36"
        //         Layout.fillWidth: true
        //         elide: Text.ElideRight
        //     }

        //     Label {
        //         text: qsTr("%1°").arg(Math.round(indicator._renderAzimuthDeg))
        //         color: "#5a6270"
        //     }
        // }

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

                // Компасная ориентация: 0° вверх, 90° вправо, 180° вниз, 270° влево
                // У Qt rotation: 0° вправо, +CCW => чтобы 0° стало вверх, делаем -90.
                readonly property real baseRotation: -90

                // Dial base
                Rectangle {
                    anchors.fill: parent
                    color: "#eef1f5"
                    border.color: "#9aa3b1"
                    border.width: 1
                    radius: width / 2
                }

                // Labels
                Text {
                    text: "0"
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: 6
                    color: "#5a6270"
                }
                Text {
                    text: "90"
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    anchors.rightMargin: 6
                    color: "#5a6270"
                }
                Text {
                    text: "180"
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 6
                    color: "#5a6270"
                }
                Text {
                    text: "270"
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 6
                    color: "#5a6270"
                }

                // Minor ticks (every 30°)
                Repeater {
                    model: 12
                    Rectangle {
                        width: 2
                        height: dial.rOuter * 0.06
                        radius: 1
                        color: "#9aa3b1"
                        x: (dial.width - width) / 2
                        y: 6
                        transform: Rotation {
                            origin.x: width / 2
                            origin.y: dial.rOuter - 6
                            angle: index * 30
                        }
                        opacity: 0.75
                    }
                }

                // --- Antenna beam: two half-sectors ---------------------------
                Item {
                    id: beamLayer
                    anchors.fill: parent
                    // Входной азимут считаем по часовой (как на компасе),
                    // поэтому: baseRotation + azimuth
                    rotation: dial.baseRotation + indicator._renderAzimuthDeg
                    transformOrigin: Item.Center

                    // Правая половина (зелёная): 0 .. +beamHalf
                    Shape {
                        anchors.fill: parent
                        layer.enabled: true
                        layer.smooth: true
                        antialiasing: true
                        opacity: indicator.beamOpacity

                        ShapePath {
                            fillColor: "#2ecc71"
                            strokeColor: "transparent"
                            strokeWidth: 0

                            PathMove {
                                x: dial.width / 2 + dial.rInner
                                y: dial.height / 2
                            }
                            PathArc {
                                x: dial.width / 2 + dial.rInner * Math.cos(dial.beamHalf * Math.PI / 180)
                                y: dial.height / 2 + dial.rInner * Math.sin(dial.beamHalf * Math.PI / 180)
                                radiusX: dial.rInner
                                radiusY: dial.rInner
                                useLargeArc: false
                                direction: PathArc.Clockwise
                            }
                            PathLine {
                                x: dial.width / 2 + dial.rOuter * Math.cos(dial.beamHalf * Math.PI / 180)
                                y: dial.height / 2 + dial.rOuter * Math.sin(dial.beamHalf * Math.PI / 180)
                            }
                            PathArc {
                                x: dial.width / 2 + dial.rOuter
                                y: dial.height / 2
                                radiusX: dial.rOuter
                                radiusY: dial.rOuter
                                useLargeArc: false
                                direction: PathArc.Counterclockwise
                            }
                        }
                    }

                    // Левая половина (красная): -beamHalf .. 0
                    Shape {
                        anchors.fill: parent
                        layer.enabled: true
                        layer.smooth: true
                        antialiasing: true
                        opacity: indicator.beamOpacity

                        ShapePath {
                            fillColor: "#e74c3c"
                            strokeColor: "transparent"
                            strokeWidth: 0

                            PathMove {
                                x: dial.width / 2 + dial.rInner * Math.cos(-dial.beamHalf * Math.PI / 180)
                                y: dial.height / 2 + dial.rInner * Math.sin(-dial.beamHalf * Math.PI / 180)
                            }
                            PathArc {
                                x: dial.width / 2 + dial.rInner
                                y: dial.height / 2
                                radiusX: dial.rInner
                                radiusY: dial.rInner
                                useLargeArc: false
                                direction: PathArc.Clockwise
                            }
                            PathLine {
                                x: dial.width / 2 + dial.rOuter
                                y: dial.height / 2
                            }
                            PathArc {
                                x: dial.width / 2 + dial.rOuter * Math.cos(-dial.beamHalf * Math.PI / 180)
                                y: dial.height / 2 + dial.rOuter * Math.sin(-dial.beamHalf * Math.PI / 180)
                                radiusX: dial.rOuter
                                radiusY: dial.rOuter
                                useLargeArc: false
                                direction: PathArc.Counterclockwise
                            }
                        }
                    }

                    // Контур сектора (опционально)
                    Shape {
                        anchors.fill: parent
                        antialiasing: true
                        opacity: 0.45

                        ShapePath {
                            strokeColor: "#2b2f36"
                            strokeWidth: 1
                            fillColor: "transparent"

                            PathMove {
                                x: dial.width / 2 + dial.rInner * Math.cos(-dial.beamHalf * Math.PI / 180)
                                y: dial.height / 2 + dial.rInner * Math.sin(-dial.beamHalf * Math.PI / 180)
                            }
                            PathLine {
                                x: dial.width / 2 + dial.rOuter * Math.cos(-dial.beamHalf * Math.PI / 180)
                                y: dial.height / 2 + dial.rOuter * Math.sin(-dial.beamHalf * Math.PI / 180)
                            }
                            PathArc {
                                x: dial.width / 2 + dial.rOuter * Math.cos(dial.beamHalf * Math.PI / 180)
                                y: dial.height / 2 + dial.rOuter * Math.sin(dial.beamHalf * Math.PI / 180)
                                radiusX: dial.rOuter
                                radiusY: dial.rOuter
                                useLargeArc: false
                                direction: PathArc.Clockwise
                            }
                            PathLine {
                                x: dial.width / 2 + dial.rInner * Math.cos(dial.beamHalf * Math.PI / 180)
                                y: dial.height / 2 + dial.rInner * Math.sin(dial.beamHalf * Math.PI / 180)
                            }
                            PathArc {
                                x: dial.width / 2 + dial.rInner * Math.cos(-dial.beamHalf * Math.PI / 180)
                                y: dial.height / 2 + dial.rInner * Math.sin(-dial.beamHalf * Math.PI / 180)
                                radiusX: dial.rInner
                                radiusY: dial.rInner
                                useLargeArc: false
                                direction: PathArc.Counterclockwise
                            }
                        }
                    }
                }

                // Центр
                Rectangle {
                    width: dial.width * 0.05
                    height: width
                    radius: width / 2
                    color: "#2b2f36"
                    anchors.centerIn: parent
                    opacity: 0.85
                }

                // Внутреннее кольцо
                Rectangle {
                    width: dial.rInner * 2
                    height: width
                    radius: width / 2
                    anchors.centerIn: parent
                    color: "transparent"
                    border.color: "#9aa3b1"
                    border.width: 1
                    opacity: 0.65
                }
            }
        }
    }
}
