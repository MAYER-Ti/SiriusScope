import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "components/AntennaIndicator" as AntInd
import "components" as Components

ApplicationWindow {
    width: 1080
    height: 640
    visible: true
    title: qsTr("SiriusScope")
    font.pixelSize: 12

    menuBar: Components.MenuBarApp { }

    RowLayout {
        anchors.fill: parent
        spacing: 4

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.horizontalStretchFactor: 3
            Layout.minimumWidth: 400
            spacing: 4

            Components.SpectrumView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.verticalStretchFactor: 2
                Layout.minimumHeight: 120
            }

            Components.WaterfallView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.verticalStretchFactor: 8
                Layout.minimumHeight: 200
            }
        }

        AntInd.AntennaIndicator {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.horizontalStretchFactor: 2
            Layout.minimumWidth: 280
        }
    }

    footer: Components.FooterDataView { }
}
