import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes

Item {
    id: antennaIndicator
    // Вход: обновляется хоть каждые 10 мс (0..359.9)
    property real azimuthDeg: 0

    Timer {
        id: timerSendAzimuth
        interval: 100
        running: true
        repeat: true
        onTriggered: {
            antennaIndicator.azimuthDeg = (antennaIndicator.azimuthDeg + 0.9) % 360
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
                text: "◀"
                font.pixelSize: 24

            }
            Button {
                id: buttonTurnStop
                Layout.fillWidth: true
                Layout.minimumHeight: 40
                text: "■"
                font.pixelSize: 18
            }
            Button {
                id: buttonTurnRight
                Layout.fillWidth: true
                Layout.minimumHeight: 40
                text: "▶"
                font.pixelSize: 24
            }
        }
    }



}

