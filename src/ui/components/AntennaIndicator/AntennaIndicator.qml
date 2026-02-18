import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SiriusScope 1.0

Item {
    id: antennaIndicator
    property real azimuthDeg: 0 // Input azimuth value in range 0..359.9
    property int turnDirection: 0 // -1: left, 0: stop, 1: right
    property real turnSpeedPerSec: 5
    property var liveBearings: []
    readonly property bool isTestState: (AppState.mode === AppState.Test)
    readonly property var activeBearings: isTestState ? testBearings : liveBearings

    onIsTestStateChanged: {
        antennaIndicator.turnDirection = 0
        antennaIndicator.testBearings = []
        antennaIndicator._testLastMs = 0
        if (indicator) {
            indicator.resetTargets()
        }
    }

    Timer {
        id: timerSendAzimuth
        interval: 100
        running: antennaIndicator.isTestState && antennaIndicator.turnDirection !== 0
        repeat: true
        onTriggered: {
            var dt = interval / 1000.0
            antennaIndicator.azimuthDeg = (antennaIndicator.azimuthDeg +
                                           antennaIndicator.turnDirection * antennaIndicator.turnSpeedPerSec * dt + 360) % 360
        }
    }

    property real testSectorHalfDeg: 25
    property var testBaseOffsetsDeg: [-18, -6, 8, 20]
    property var testOffsetVelDegPerSec: [0.12, -0.08, 0.00, 0.10]
    property real testJitterDeg: 0.8
    property real testDropProb: 0.10
    property real testClutterProb: 0.15
    property int testClutterCount: 2
    property real _testLastMs: 0
    property var testBearings: []
    function norm360(deg) {
        var a = deg % 360
        if (a < 0) a += 360
        return a
    }
    Timer {
        id: testTargetsTimer
        interval: 250
        running: antennaIndicator.isTestState
        repeat: true

        onTriggered: {
            var now = Date.now()
            var dt = (antennaIndicator._testLastMs > 0)
                ? (now - antennaIndicator._testLastMs) / 1000.0
                : interval / 1000.0
            antennaIndicator._testLastMs = now

            var arr = []

            for (var i = 0; i < antennaIndicator.testBaseOffsetsDeg.length; i++) {
                var offset = antennaIndicator.testBaseOffsetsDeg[i]
                var vel = 0
                if (i < antennaIndicator.testOffsetVelDegPerSec.length) {
                    vel = antennaIndicator.testOffsetVelDegPerSec[i]
                }

                offset = offset + vel * dt
                if (offset > antennaIndicator.testSectorHalfDeg) {
                    offset = antennaIndicator.testSectorHalfDeg
                    antennaIndicator.testOffsetVelDegPerSec[i] = -Math.abs(vel)
                } else if (offset < -antennaIndicator.testSectorHalfDeg) {
                    offset = -antennaIndicator.testSectorHalfDeg
                    antennaIndicator.testOffsetVelDegPerSec[i] = Math.abs(vel)
                }
                antennaIndicator.testBaseOffsetsDeg[i] = offset

                if (Math.random() >= antennaIndicator.testDropProb) {
                    var jitter = (Math.random() * 2 - 1) * antennaIndicator.testJitterDeg
                    arr.push(norm360(antennaIndicator.azimuthDeg + offset + jitter))
                }
            }

            if (Math.random() < antennaIndicator.testClutterProb) {
                for (var c = 0; c < antennaIndicator.testClutterCount; c++) {
                    var clutterOffset = (Math.random() * 2 - 1) * antennaIndicator.testSectorHalfDeg
                    arr.push(norm360(antennaIndicator.azimuthDeg + clutterOffset))
                }
            }

            antennaIndicator.testBearings = arr
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 4

        Indicator {
            id: indicator
            azimuthDeg: antennaIndicator.azimuthDeg
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.verticalStretchFactor: 8
            Layout.minimumHeight: 120
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.verticalStretchFactor: 2
            spacing: 4

            Button {
                id: buttonTurnLeft
                Layout.fillWidth: true
                Layout.minimumHeight: 40
                text: "\u25C0"
                font.pixelSize: 24
                enabled: true
                opacity: 1.0
                onClicked: {
                    if (AppState.mode === AppState.Test) {
                        antennaIndicator.turnDirection = -1
                    } else if (AppState.mode === AppState.Combat) {
                        console.log("TODO: handle turn left in Combat mode")
                    } else if (AppState.mode === AppState.Control) {
                        console.log("TODO: handle turn left in Control mode")
                    }
                }
            }

            Button {
                id: buttonTurnStop
                Layout.fillWidth: true
                Layout.minimumHeight: 40
                text: "\u25A0"
                font.pixelSize: 18
                enabled: true
                opacity: 1.0
                onClicked: {
                    if (AppState.mode === AppState.Test) {
                        antennaIndicator.turnDirection = 0
                    } else if (AppState.mode === AppState.Combat) {
                        console.log("TODO: handle stop turn in Combat mode")
                    } else if (AppState.mode === AppState.Control) {
                        console.log("TODO: handle stop turn in Control mode")
                    }
                }
            }

            Button {
                id: buttonTurnRight
                Layout.fillWidth: true
                Layout.minimumHeight: 40
                text: "\u25B6"
                font.pixelSize: 24
                enabled: true
                opacity: 1.0
                onClicked: {
                    if (AppState.mode === AppState.Test) {
                        antennaIndicator.turnDirection = 1
                    } else if (AppState.mode === AppState.Combat) {
                        console.log("TODO: handle turn right in Combat mode")
                    } else if (AppState.mode === AppState.Control) {
                        console.log("TODO: handle turn right in Control mode")
                    }
                }
            }
        }
    }
}
