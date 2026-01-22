import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property real azimuthDeg: 0

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

        Label {
            text: qsTr("Пеленгатор (заготовка)")
            font.bold: true
            color: "#2b2f36"
        }

        Label {
            text: qsTr("Azimuth: %1°").arg(Math.round(root.azimuthDeg))
            color: "#5a6270"
        }

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

                Rectangle {
                    anchors.fill: parent
                    color: "#eef1f5"
                    border.color: "#9aa3b1"
                    border.width: 1
                    radius: width / 2
                }

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

                Rectangle {
                    id: pointer
                    width: 4
                    height: dial.height * 0.42
                    color: "#2f4b66"
                    x: (dial.width - width) / 2
                    y: (dial.height / 2) - height
                    radius: 2
                    transform: Rotation {
                        origin.x: pointer.width / 2
                        origin.y: pointer.height
                        angle: root.azimuthDeg
                    }
                }

                Rectangle {
                    width: 8
                    height: 8
                    radius: 4
                    color: "#2f4b66"
                    anchors.centerIn: parent
                }
            }
        }
    }
}
