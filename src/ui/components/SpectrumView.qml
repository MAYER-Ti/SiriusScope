import QtQuick
import QtQuick.Controls

Item {
    id: root

    Rectangle {
        anchors.fill: parent
        //color: "#0f1115"
        border.color: "#2b2f36"
        border.width: 1
        radius: 6

        Column {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 6

            Row {
                spacing: 8

                Rectangle {
                    width: 10
                    height: 10
                    radius: 2
                    color: "#4ea1ff"
                }

                Text {
                    text: qsTr("SpectrumView")
                    //color: "#d7dbe2"
                    font.bold: true
                }
            }

            Rectangle {
                width: parent.width
                height: parent.height - 24
                //color: "#141922"
                //border.color: "#243041"
                border.width: 1
                radius: 4

                Text {
                    anchors.centerIn: parent
                    text: qsTr("Заготовка для сложного виджета")
                    //color: "#8b93a1"
                    font.pixelSize: 11
                }
            }
        }
    }
}
