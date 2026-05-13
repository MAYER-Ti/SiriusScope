import QtQuick
import QtQuick.Layouts
import SiriusScope 1.0

Rectangle {
    id: root

    implicitHeight: 56
    color: Theme.panelBottom
    border.color: Theme.panelBorder
    border.width: 1

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.pageMargin
        anchors.rightMargin: Theme.pageMargin
        anchors.topMargin: 10
        anchors.bottomMargin: 10
        spacing: 10

        StatusChip {
            Layout.preferredWidth: 146
            label: qsTr("Программа")
            value: "OK"
            statusColor: Theme.statusGood
        }

        StatusChip {
            Layout.preferredWidth: 124
            label: qsTr("БЦО")
            value: qsTr("связь")
            statusColor: Theme.statusGood
        }

        StatusChip {
            Layout.preferredWidth: 158
            label: qsTr("Антенна")
            value: "034.7°"
            statusColor: Theme.statusGood
        }

        StatusChip {
            Layout.preferredWidth: 156
            label: qsTr("Запись")
            value: qsTr("активна")
            statusColor: Theme.statusWarn
        }

        StatusChip {
            Layout.fillWidth: true
            Layout.minimumWidth: 220
            label: qsTr("Диагностика")
            value: "2 dropped rows, storage lag 18 ms"
            statusColor: Theme.statusWarn
        }

        StatusChip {
            Layout.preferredWidth: 196
            label: ""
            value: "sampleIndex preserved"
            statusColor: Theme.statusGood
        }
    }
}
