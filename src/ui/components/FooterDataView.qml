import QtQuick
import QtQuick.Layouts
import SiriusScope 1.0

Rectangle {
    id: root

    implicitHeight: 86
    color: Theme.panelBottom
    border.color: Theme.panelBorder
    border.width: 1

    function statusColor(level) {
        if (level === StatusModel.Good) {
            return Theme.statusGood
        }
        if (level === StatusModel.Warning) {
            return Theme.statusWarn
        }
        if (level === StatusModel.Error) {
            return Theme.statusBad
        }
        return Theme.textVeryMuted
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
            value: StatusModel.programValue
            statusColor: root.statusColor(StatusModel.programLevel)
        }

        StatusChip {
            Layout.fillWidth: true
            label: qsTr("Режим")
            value: StatusModel.modeValue
            statusColor: root.statusColor(StatusModel.modeLevel)
        }

        StatusChip {
            Layout.fillWidth: true
            label: qsTr("БЦО")
            value: StatusModel.bcoValue
            statusColor: root.statusColor(StatusModel.bcoLevel)
        }

        StatusChip {
            Layout.fillWidth: true
            label: qsTr("БЦО упр.")
            value: StatusModel.bcoControlValue
            statusColor: root.statusColor(StatusModel.bcoControlLevel)
        }

        StatusChip {
            Layout.fillWidth: true
            label: qsTr("Поворот")
            value: StatusModel.antennaValue
            statusColor: root.statusColor(StatusModel.antennaLevel)
        }

        StatusChip {
            Layout.fillWidth: true
            label: qsTr("Азимут")
            value: StatusModel.azimuthValue
            statusColor: root.statusColor(StatusModel.azimuthLevel)
        }

        StatusChip {
            Layout.fillWidth: true
            label: qsTr("Запись")
            value: StatusModel.recordingValue
            statusColor: root.statusColor(StatusModel.recordingLevel)
        }

        StatusChip {
            Layout.fillWidth: true
            label: qsTr("Диагностика")
            value: StatusModel.diagnosticsValue
            statusColor: root.statusColor(StatusModel.diagnosticsLevel)
        }
    }
}
