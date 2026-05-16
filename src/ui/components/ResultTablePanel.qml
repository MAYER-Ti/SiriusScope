import QtQuick
import QtQuick.Layouts
import SiriusScope 1.0

Panel {
    id: root

    contentMargins: 12
    implicitHeight: 184

    readonly property real tableWeight: 740

    function statusColor(status) {
        return status === qsTr("Запись") ? Theme.statusWarn : Theme.statusGood
    }

    ListModel {
        id: resultRows
        ListElement { time: "18:24:02"; azimuth: "034,1"; band: "Диапазон 1"; bandIndex: 0; frequency: "3,000"; amplitude: "118"; status: "Готово" }
        ListElement { time: "18:24:05"; azimuth: "036,8"; band: "Диапазон 3"; bandIndex: 2; frequency: "8,250"; amplitude: "124"; status: "Готово" }
        ListElement { time: "18:24:11"; azimuth: "031,7"; band: "Диапазон 5"; bandIndex: 4; frequency: "14,250"; amplitude: "109"; status: "Запись" }
        ListElement { time: "18:24:18"; azimuth: "040,2"; band: "Диапазон 2"; bandIndex: 1; frequency: "5,795"; amplitude: "116"; status: "Готово" }
    }

    ColumnLayout {
        anchors.fill: root.contentItem
        spacing: 8

        Text {
            Layout.fillWidth: true
            Layout.preferredHeight: 24
            Layout.maximumHeight: 28
            text: qsTr("Результаты")
            color: Theme.textPrimary
            font.pixelSize: Theme.fontMedium
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

            Flickable {
                id: tableFlick
                anchors.fill: parent
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                contentWidth: Math.max(width, root.tableWeight)
                contentHeight: height

                Item {
                    id: tableContent
                    width: tableFlick.contentWidth
                    height: tableFlick.height

                    Row {
                        id: headerRow
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        height: 32

                        HeaderCell { width: tableContent.width * 105 / root.tableWeight; title: qsTr("Время") }
                        HeaderCell { width: tableContent.width * 120 / root.tableWeight; title: qsTr("Азимут") }
                        HeaderCell { width: tableContent.width * 150 / root.tableWeight; title: qsTr("Диапазон") }
                        HeaderCell { width: tableContent.width * 130 / root.tableWeight; title: qsTr("Частота") }
                        HeaderCell { width: tableContent.width * 115 / root.tableWeight; title: qsTr("Амплитуда") }
                        HeaderCell { width: tableContent.width * 120 / root.tableWeight; title: qsTr("Состояние") }
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
                            height: 32

                            readonly property color rowText: index % 2 === 0 ? Theme.textSecondary : Theme.textMuted
                            readonly property color rowFill: index % 2 === 0 ? "#0E141B" : Theme.insetBackground

                            DataCell {
                                width: resultList.width * 105 / root.tableWeight
                                value: model.time
                                textColor: rowText
                                fillColor: rowFill
                            }
                            DataCell {
                                width: resultList.width * 120 / root.tableWeight
                                value: model.azimuth
                                textColor: rowText
                                fillColor: rowFill
                            }
                            DataCell {
                                width: resultList.width * 150 / root.tableWeight
                                value: model.band
                                textColor: Theme.bandColor(model.bandIndex)
                                fillColor: rowFill
                                bold: true
                            }
                            DataCell {
                                width: resultList.width * 130 / root.tableWeight
                                value: model.frequency
                                textColor: rowText
                                fillColor: rowFill
                            }
                            DataCell {
                                width: resultList.width * 115 / root.tableWeight
                                value: model.amplitude
                                textColor: rowText
                                fillColor: rowFill
                            }
                            DataCell {
                                width: resultList.width * 120 / root.tableWeight
                                value: model.status
                                textColor: root.statusColor(model.status)
                                fillColor: rowFill
                                bold: true
                            }
                        }
                    }
                }
            }
        }
    }

    component HeaderCell: Rectangle {
        property string title: ""

        height: 32
        color: Theme.chipBackground
        border.color: Theme.divider
        border.width: 1

        Text {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 4
            text: title
            color: Theme.textLabel
            font.pixelSize: Theme.fontSmall
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

        height: 32
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
            font.pixelSize: Theme.fontSmall
            font.weight: bold ? Font.DemiBold : Font.Normal
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }
}
