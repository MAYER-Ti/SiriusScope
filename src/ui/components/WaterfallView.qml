import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property bool running: false
    property bool busy: false
    property real fps: 0
    property real centerFrequency: 0

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
            text: qsTr("Водопад (заготовка)")
            font.bold: true
            color: "#2b2f36"
        }

        Label {
            text: qsTr("Здесь будет спектрально-временная визуализация.")
            color: "#5a6270"
            wrapMode: Text.WordWrap
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}
