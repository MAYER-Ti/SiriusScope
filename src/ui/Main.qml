import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SiriusScope 1.0
import "components/AntennaIndicator" as AntInd
import "components/SpectrumView" as SpecView
import "components" as Components

ApplicationWindow {
    width: Math.max(minimumWidth, Screen.width * 0.72)
    height: Math.max(minimumHeight, Screen.height * 0.74)
    minimumWidth: 1080
    minimumHeight: 720
    visible: true
    title: qsTr("SiriusScope")
    font.pixelSize: Theme.fontNormal
    color: Theme.appBackgroundDeep

    menuBar: Components.MenuBarApp { }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.appBackgroundTop }
            GradientStop { position: 1.0; color: Theme.appBackgroundBottom }
        }
        border.color: "#27313C"
        border.width: 1
        radius: 8

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Theme.pageMargin
            spacing: 12

            Components.TopToolbar {
                id: topToolbar

                Layout.fillWidth: true
                Layout.preferredHeight: 52
                Layout.minimumHeight: 52
                Layout.maximumHeight: 58
                canScanSector: antennaIndicator.hasSelectedSector

                onLiveRequested: WaterfallController.jumpToLive()
                onScanSectorRequested: AntennaController.scan(
                    antennaIndicator.selectedLeftAngle,
                    antennaIndicator.selectedRightAngle,
                    antennaIndicator.scanSpeed)
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 12

                Components.Panel {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.horizontalStretchFactor: 63
                    Layout.minimumWidth: 640
                    contentMargins: 18

                    ColumnLayout {
                        anchors.fill: parent.contentItem
                        spacing: 18

                        SpecView.SpectrumView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.verticalStretchFactor: 31
                            Layout.minimumHeight: 170
                        }

                        Components.WaterfallView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.verticalStretchFactor: 61
                            Layout.minimumHeight: 280
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.horizontalStretchFactor: 34
                    Layout.minimumWidth: 360
                    spacing: 14

                    AntInd.AntennaIndicator {
                        id: antennaIndicator

                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.verticalStretchFactor: 72
                        Layout.minimumHeight: 330
                        azimuthDeg: AntennaController.azimuthDeg
                        targetAzimuthsDeg: []

                        onStopRequested: AntennaController.stop()
                        onDriveLeftRequested: function(speed) {
                            AntennaController.driveLeft(speed)
                        }
                        onDriveRightRequested: function(speed) {
                            AntennaController.driveRight(speed)
                        }
                        onScanRequested: function(leftAngle, rightAngle, speed) {
                            AntennaController.scan(leftAngle, rightAngle, speed)
                        }
                    }

                    Components.ResultTablePanel {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.verticalStretchFactor: 26
                        Layout.minimumHeight: 150
                    }
                }
            }
        }
    }

    footer: Components.FooterDataView { }
}
