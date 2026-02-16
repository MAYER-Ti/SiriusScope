import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SiriusScope 1.0

Item {
    id: antennaIndicator
    property real azimuthDeg: 0 // Input azimuth value in range 0..359.9
    property int turnDirection: 0 // -1: left, 0: stop, 1: right
    property real turnSpeedPerSec: 5
    readonly property bool isTestState: (AppState.mode === AppState.Test)

    onIsTestStateChanged: {
        if (!isTestState) {
            antennaIndicator.turnDirection = 0
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
                onClicked: {
                    if (antennaIndicator.isTestState) {
                        antennaIndicator.turnDirection = -1
                    }
                }
            }

            Button {
                id: buttonTurnStop
                Layout.fillWidth: true
                Layout.minimumHeight: 40
                text: "\u25A0"
                font.pixelSize: 18
                onClicked: {
                    if (antennaIndicator.isTestState) {
                        antennaIndicator.turnDirection = 0
                    }
                }
            }

            Button {
                id: buttonTurnRight
                Layout.fillWidth: true
                Layout.minimumHeight: 40
                text: "\u25B6"
                font.pixelSize: 24
                onClicked: {
                    if (antennaIndicator.isTestState) {
                        antennaIndicator.turnDirection = 1
                    }
                }
            }
        }
    }
}
