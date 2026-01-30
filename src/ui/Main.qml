import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
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
            Layout.preferredWidth: parent.width * 0.6
            Layout.minimumWidth: 400
            spacing: 4

            Components.SpectrumView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: parent.height * 0.2
                Layout.minimumHeight: 120
            }

            Components.WaterfallView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: parent.height * 0.8
                Layout.minimumHeight: 200
            }
        }

        Components.AntennaIndicator {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: parent.width * 0.4
            Layout.minimumWidth: 280
        }
    }

    footer: Components.FooterDataView { }
}
