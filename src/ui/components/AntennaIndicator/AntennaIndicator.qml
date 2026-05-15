import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SiriusScope 1.0
import ".." as Components

Components.Panel {
    id: antennaIndicator

    property real azimuthDeg: 0
    property var targetAzimuthsDeg: []
    property bool hasSelectedSector: false
    property real selectedLeftAngle: 0
    property real selectedRightAngle: 0
    property int scanSpeed: 10

    readonly property int targetCount: targetAzimuthsDeg ? targetAzimuthsDeg.length : 0
    readonly property string selectedSectorText: hasSelectedSector
        ? selectedLeftAngle.toFixed(0) + "°→" + selectedRightAngle.toFixed(0) + "°"
        : "—"

    signal stopRequested()
    signal driveLeftRequested(int speed)
    signal driveRightRequested(int speed)
    signal scanRequested(real leftAngle, real rightAngle, int speed)

    contentMargins: 18

    component ControlButton: Button {
        id: control

        property color normalColor: Theme.chipBackground
        property color hoveredColor: "#203040"
        property color pressedColor: "#24455E"
        property color disabledColor: "#121922"
        property color normalBorderColor: Theme.panelBorder
        property color hoveredBorderColor: "#3B4E60"
        property color pressedBorderColor: Theme.signalCyan
        property color disabledBorderColor: Theme.panelBorderSoft
        property color textColor: Theme.textPrimary
        property color disabledTextColor: Theme.textVeryMuted

        hoverEnabled: true
        scale: down ? 0.97 : 1.0

        Behavior on scale {
            NumberAnimation { duration: 80 }
        }

        contentItem: Text {
            text: control.text
            color: control.enabled ? control.textColor : control.disabledTextColor
            font: control.font
            horizontalAlignment: Text.AlignCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            radius: Theme.radiusInset
            color: !control.enabled
                   ? control.disabledColor
                   : control.down
                     ? control.pressedColor
                     : control.hovered
                       ? control.hoveredColor
                       : control.normalColor
            border.color: !control.enabled
                          ? control.disabledBorderColor
                          : control.down
                            ? control.pressedBorderColor
                            : control.hovered
                              ? control.hoveredBorderColor
                              : control.normalBorderColor
            opacity: control.enabled ? 1.0 : 0.56

            Behavior on color {
                ColorAnimation { duration: 90 }
            }
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
                    { label: "Speed", value: antennaIndicator.scanSpeed.toString() + "°/s", color: Theme.textSecondary, weight: 112 },
                    { label: "Targets", value: antennaIndicator.targetCount.toString(), color: Theme.textSecondary, weight: 98 },
                    { label: "Sector", value: antennaIndicator.selectedSectorText, color: Theme.textSecondary, weight: 132 },
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
            targetAzimuthsDeg: antennaIndicator.targetAzimuthsDeg
            hasSelectedSector: antennaIndicator.hasSelectedSector
            selectedLeftAngle: antennaIndicator.selectedLeftAngle
            selectedRightAngle: antennaIndicator.selectedRightAngle
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 260
            beamWidthDeg: 60

            onSectorSelected: function(leftAngle, rightAngle) {
                antennaIndicator.selectedLeftAngle = leftAngle
                antennaIndicator.selectedRightAngle = rightAngle
                antennaIndicator.hasSelectedSector = true
            }

            onSectorCleared: {
                antennaIndicator.hasSelectedSector = false
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 104
            Layout.minimumHeight: 96
            radius: Theme.radiusPanel
            color: Theme.insetBackground
            border.color: Theme.panelBorder

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 7

                Label {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 14
                    text: qsTr("Управление поворотом")
                    color: Theme.textVeryMuted
                    font.pixelSize: 10
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 30
                    spacing: 8

                    Repeater {
                        model: [
                            { label: "←10", speed: 10, direction: -1, stop: false },
                            { label: "←1", speed: 1, direction: -1, stop: false },
                            { label: "■", speed: 0, direction: 0, stop: true },
                            { label: "1→", speed: 1, direction: 1, stop: false },
                            { label: "10→", speed: 10, direction: 1, stop: false }
                        ]

                        ControlButton {
                            Layout.fillWidth: true
                            Layout.preferredHeight: modelData.stop ? 32 : 30
                            text: modelData.label
                            font.family: Theme.monoFontFamily
                            font.pixelSize: modelData.stop ? 13 : 11
                            textColor: modelData.stop ? Theme.statusBad : Theme.textPrimary
                            normalColor: modelData.stop ? "#2A1E20" : Theme.chipBackground
                            hoveredColor: modelData.stop ? "#35282B" : "#203040"
                            pressedColor: modelData.stop ? "#452E32" : "#24455E"
                            normalBorderColor: modelData.stop ? Theme.statusBad : Theme.panelBorder
                            hoveredBorderColor: modelData.stop ? "#FF8A83" : "#3B4E60"
                            pressedBorderColor: modelData.stop ? "#FFB0AA" : Theme.signalCyan

                            onPressed: {
                                if (modelData.stop) {
                                    antennaIndicator.stopRequested()
                                } else if (modelData.direction < 0) {
                                    antennaIndicator.driveLeftRequested(modelData.speed)
                                } else {
                                    antennaIndicator.driveRightRequested(modelData.speed)
                                }
                            }
                        }
                    }
                }

                ControlButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 30
                    enabled: antennaIndicator.hasSelectedSector
                    text: qsTr("Сканировать сектор")
                    font.pixelSize: 11
                    normalColor: "#123044"
                    hoveredColor: "#173D56"
                    pressedColor: "#1A4D6C"
                    disabledColor: Theme.chipBackground
                    normalBorderColor: Theme.signalCyan
                    hoveredBorderColor: "#7DCBEE"
                    pressedBorderColor: "#B8E8FF"
                    disabledBorderColor: Theme.panelBorder

                    onClicked: antennaIndicator.scanRequested(
                        antennaIndicator.selectedLeftAngle,
                        antennaIndicator.selectedRightAngle,
                        antennaIndicator.scanSpeed)
                }
            }
        }
    }
}
