import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SiriusScope 1.0
import ".." as Components

Components.Panel {
    id: antennaIndicator

    property real azimuthDeg: 34.7
    property int turnDirection: 0
    property real turnSpeedPerSec: 5
    property var liveBearings: []
    readonly property bool isTestState: (AppState.mode === AppState.Test)
    readonly property var activeBearings: isTestState ? testBearings : liveBearings

    contentMargins: 18

    function norm360(deg) {
        var a = deg % 360
        if (a < 0) a += 360
        return a
    }

    function handleManualStep(deltaDeg) {
        if (AppState.mode === AppState.Test) {
            antennaIndicator.turnDirection = 0
            antennaIndicator.azimuthDeg = norm360(antennaIndicator.azimuthDeg + deltaDeg)
        } else if (AppState.mode === AppState.Combat) {
            console.log("TODO: handle antenna step in Combat mode", deltaDeg)
        } else if (AppState.mode === AppState.Control) {
            console.log("TODO: handle antenna step in Control mode", deltaDeg)
        }
    }

    function stopManualTurn() {
        if (AppState.mode === AppState.Test) {
            antennaIndicator.turnDirection = 0
        } else if (AppState.mode === AppState.Combat) {
            console.log("TODO: handle antenna stop in Combat mode")
        } else if (AppState.mode === AppState.Control) {
            console.log("TODO: handle antenna stop in Control mode")
        }
    }

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
            antennaIndicator.azimuthDeg = norm360(antennaIndicator.azimuthDeg +
                                                  antennaIndicator.turnDirection *
                                                  antennaIndicator.turnSpeedPerSec * dt)
        }
    }

    property real testSectorHalfDeg: 25
    property var testBaseOffsetsDeg: [-18, -6, 8, 20, 106]
    property var testOffsetVelDegPerSec: [0.12, -0.08, 0.00, 0.10, 0.04]
    property real testJitterDeg: 0.8
    property real testDropProb: 0.10
    property real testClutterProb: 0.15
    property int testClutterCount: 1
    property real _testLastMs: 0
    property var testBearings: [12, 26, 41, 63, 141]

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
                if (offset > 150) {
                    offset = 150
                    antennaIndicator.testOffsetVelDegPerSec[i] = -Math.abs(vel)
                } else if (offset < -150) {
                    offset = -150
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
        anchors.fill: antennaIndicator.contentItem
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 24
            spacing: 8

            Repeater {
                model: [
                    { label: "Speed", value: antennaIndicator.turnSpeedPerSec.toFixed(0) + "°/s", color: Theme.textSecondary, weight: 112 },
                    { label: "Tracks", value: antennaIndicator.activeBearings.length.toString(), color: Theme.textSecondary, weight: 98 },
                    { label: "TTL", value: "12s", color: Theme.textSecondary, weight: 86 },
                    { label: "AZ", value: antennaIndicator.azimuthDeg.toFixed(1) + "°", color: Theme.textPrimary, weight: 132 }
                ]

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredWidth: modelData.weight
                    Layout.preferredHeight: 24
                    radius: Theme.radiusInset
                    color: Theme.chipBackground
                    border.color: Theme.panelBorder

                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: 9
                        anchors.rightMargin: 8
                        text: modelData.label + " " + modelData.value
                        color: modelData.color
                        font.family: Theme.monoFontFamily
                        font.pixelSize: 10
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                }
            }
        }

        Indicator {
            id: indicator
            azimuthDeg: antennaIndicator.azimuthDeg
            targetAzimuthsDeg: antennaIndicator.activeBearings
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 260
            beamWidthDeg: 60
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            Layout.minimumHeight: 42
            radius: Theme.radiusPanel
            color: Theme.insetBackground
            border.color: Theme.panelBorder

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.top: parent.top
                anchors.topMargin: 4
                text: qsTr("ручное наведение")
                color: Theme.textVeryMuted
                font.pixelSize: 9
            }

            RowLayout {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                anchors.bottomMargin: 7
                height: 28
                spacing: 10

                Repeater {
                    model: [
                        { label: "-10°", delta: -10, stop: false },
                        { label: "-1°", delta: -1, stop: false },
                        { label: "■", delta: 0, stop: true },
                        { label: "+1°", delta: 1, stop: false },
                        { label: "+10°", delta: 10, stop: false }
                    ]

                    Button {
                        Layout.fillWidth: true
                        Layout.preferredHeight: modelData.stop ? 32 : 28
                        text: modelData.label
                        font.family: Theme.monoFontFamily
                        font.pixelSize: modelData.stop ? 13 : 11
                        contentItem: Text {
                            text: parent.text
                            color: modelData.stop ? Theme.statusBad : Theme.textPrimary
                            font: parent.font
                            horizontalAlignment: Text.AlignCenter
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                        background: Rectangle {
                            radius: Theme.radiusInset
                            color: modelData.stop ? "#2A1E20" : Theme.chipBackground
                            border.color: modelData.stop ? Theme.statusBad : Theme.panelBorder
                        }
                        onClicked: {
                            if (modelData.stop) {
                                antennaIndicator.stopManualTurn()
                            } else {
                                antennaIndicator.handleManualStep(modelData.delta)
                            }
                        }
                    }
                }
            }
        }
    }
}
