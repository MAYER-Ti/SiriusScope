import QtQuick
import QtQuick.Layouts
import SiriusScope 1.0

Rectangle {
    id: root

    implicitHeight: 86
    color: Theme.panelBottom
    border.color: Theme.panelBorder
    border.width: 1

    function modeText() {
        if (AppState.mode === AppState.Test) {
            return qsTr("генератор")
        }
        if (AppState.mode === AppState.Combat) {
            return qsTr("аппаратура")
        }
        return qsTr("контроль")
    }

    function azimuthText(value) {
        var normalized = value % 360
        if (normalized < 0) {
            normalized += 360
        }
        var text = normalized.toFixed(1).replace(".", ",")
        while (text.length < 5) {
            text = "0" + text
        }
        return text + "\u00B0"
    }

    GridLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.pageMargin
        anchors.rightMargin: Theme.pageMargin
        anchors.topMargin: 8
        anchors.bottomMargin: 8
        columns: 4
        rowSpacing: 8
        columnSpacing: 10

        StatusChip {
            Layout.fillWidth: true
            label: qsTr("Программа")
            value: qsTr("работает")
            statusColor: Theme.statusGood
        }

        StatusChip {
            Layout.fillWidth: true
            label: qsTr("Режим")
            value: root.modeText()
            statusColor: Theme.statusGood
        }

        StatusChip {
            Layout.fillWidth: true
            label: qsTr("БЦО")
            value: qsTr("поток активен")
            statusColor: Theme.statusGood
        }

        StatusChip {
            Layout.fillWidth: true
            label: qsTr("РПУ")
            value: qsTr("готово")
            statusColor: Theme.statusGood
        }

        StatusChip {
            Layout.fillWidth: true
            label: qsTr("Поворот")
            value: qsTr("подключено")
            statusColor: Theme.statusGood
        }

        StatusChip {
            Layout.fillWidth: true
            label: qsTr("Азимут")
            value: root.azimuthText(AntennaController.azimuthDeg)
            statusColor: Theme.statusGood
        }

        StatusChip {
            Layout.fillWidth: true
            label: qsTr("Запись")
            value: qsTr("активна")
            statusColor: Theme.statusWarn
        }

        StatusChip {
            Layout.fillWidth: true
            label: qsTr("Диагностика")
            value: WaterfallController.historyLoading ? qsTr("загрузка истории") : qsTr("ошибок нет")
            statusColor: Theme.statusGood
        }
    }
}
