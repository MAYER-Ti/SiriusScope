import QtQuick
import QtQuick.Layouts
import SiriusScope 1.0

Panel {
    id: root

    contentMargins: 12
    implicitHeight: 184

    readonly property real tableWeight: 570

    function statusColor(status) {
        return status === "REC" ? Theme.statusWarn : Theme.statusGood
    }

    ListModel {
        id: resultRows
        ListElement { time: "18:24:02"; azimuth: "034.1"; band: "B1"; bandIndex: 0; frequency: "3.000"; amplitude: "118"; status: "OK" }
        ListElement { time: "18:24:05"; azimuth: "036.8"; band: "B3"; bandIndex: 2; frequency: "8.250"; amplitude: "124"; status: "OK" }
        ListElement { time: "18:24:11"; azimuth: "031.7"; band: "B5"; bandIndex: 4; frequency: "14.250"; amplitude: "109"; status: "REC" }
        ListElement { time: "18:24:18"; azimuth: "040.2"; band: "B2"; bandIndex: 1; frequency: "5.795"; amplitude: "116"; status: "OK" }
    }

    ColumnLayout {
        anchors.fill: root.contentItem
        spacing: 8

        Text {
            Layout.fillWidth: true
            Layout.preferredHeight: 16
            Layout.maximumHeight: 18
            text: qsTr("Итоговая таблица")
            color: Theme.textPrimary
            font.pixelSize: 12
            font.weight: Font.DemiBold
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        Rectangle {
            id: tableBody
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radiusInset
            color: Theme.insetBackground
            border.color: Theme.panelBorderSoft
            clip: true

            Row {
                id: headerRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 24

                HeaderCell { width: tableBody.width * 105 / root.tableWeight; title: qsTr("Время") }
                HeaderCell { width: tableBody.width * 85 / root.tableWeight; title: qsTr("Аз.") }
                HeaderCell { width: tableBody.width * 80 / root.tableWeight; title: "Band" }
                HeaderCell { width: tableBody.width * 115 / root.tableWeight; title: qsTr("Частота") }
                HeaderCell { width: tableBody.width * 85 / root.tableWeight; title: qsTr("Ампл.") }
                HeaderCell { width: tableBody.width * 100 / root.tableWeight; title: qsTr("Статус") }
            }

            ListView {
                id: resultList
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: headerRow.bottom
                anchors.bottom: parent.bottom
                model: resultRows
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                delegate: Row {
                    width: resultList.width
                    height: 27

                    readonly property color rowText: index % 2 === 0 ? Theme.textSecondary : Theme.textMuted
                    readonly property color rowFill: index % 2 === 0 ? "#0E141B" : Theme.insetBackground

                    DataCell {
                        width: resultList.width * 105 / root.tableWeight
                        value: model.time
                        textColor: rowText
                        fillColor: rowFill
                    }
                    DataCell {
                        width: resultList.width * 85 / root.tableWeight
                        value: model.azimuth
                        textColor: rowText
                        fillColor: rowFill
                    }
                    DataCell {
                        width: resultList.width * 80 / root.tableWeight
                        value: model.band
                        textColor: Theme.bandColor(model.bandIndex)
                        fillColor: rowFill
                        bold: true
                    }
                    DataCell {
                        width: resultList.width * 115 / root.tableWeight
                        value: model.frequency
                        textColor: rowText
                        fillColor: rowFill
                    }
                    DataCell {
                        width: resultList.width * 85 / root.tableWeight
                        value: model.amplitude
                        textColor: rowText
                        fillColor: rowFill
                    }
                    DataCell {
                        width: resultList.width * 100 / root.tableWeight
                        value: model.status
                        textColor: root.statusColor(model.status)
                        fillColor: rowFill
                        bold: true
                    }
                }
            }
        }
    }

    component HeaderCell: Rectangle {
        property string title: ""

        height: 24
        color: Theme.chipBackground
        border.color: Theme.divider
        border.width: 1

        Text {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 4
            text: title
            color: Theme.textLabel
            font.pixelSize: 10
            font.weight: Font.DemiBold
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    component DataCell: Rectangle {
        property string value: ""
        property color textColor: Theme.textSecondary
        property color fillColor: Theme.insetBackground
        property bool bold: false

        height: 27
        color: fillColor
        border.color: Theme.divider
        border.width: 1

        Text {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 4
            text: value
            color: textColor
            font.family: Theme.monoFontFamily
            font.pixelSize: 10
            font.weight: bold ? Font.DemiBold : Font.Normal
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }
}
