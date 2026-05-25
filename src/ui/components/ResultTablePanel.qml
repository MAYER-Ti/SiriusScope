import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SiriusScope 1.0

Panel {
    id: root

    contentMargins: 12
    implicitHeight: 184

    readonly property real tableWeight: 960

    function statusColor(status) {
        return status === qsTr("Диагностика") ? Theme.statusWarn : Theme.statusGood
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

                        HeaderCell { width: tableContent.width * 150 / root.tableWeight; title: qsTr("Время") }
                        HeaderCell { width: tableContent.width * 100 / root.tableWeight; title: qsTr("Пеленг") }
                        HeaderCell { width: tableContent.width * 120 / root.tableWeight; title: qsTr("Диапазон") }
                        HeaderCell { width: tableContent.width * 170 / root.tableWeight; title: qsTr("Частоты") }
                        HeaderCell { width: tableContent.width * 90 / root.tableWeight; title: qsTr("Качество") }
                        HeaderCell { width: tableContent.width * 110 / root.tableWeight; title: qsTr("Состояние") }
                        HeaderCell {
                            width: tableContent.width * 110 / root.tableWeight
                            title: qsTr("ППИ")
                            toolTip: qsTr("Период повторения импульсов (PRI)")
                        }
                        HeaderCell {
                            width: tableContent.width * 110 / root.tableWeight
                            title: qsTr("ДИ")
                            toolTip: qsTr("Длительность импульса (PW)")
                        }
                    }

                    ListView {
                        id: resultList
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: headerRow.bottom
                        anchors.bottom: parent.bottom
                        model: ResultTableModel
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds

                        delegate: Row {
                            width: resultList.width
                            height: 32

                            readonly property color rowText: index % 2 === 0 ? Theme.textSecondary : Theme.textMuted
                            readonly property color rowFill: index % 2 === 0 ? "#0E141B" : Theme.insetBackground

                            DataCell {
                                width: resultList.width * 150 / root.tableWeight
                                value: model.timeText
                                textColor: rowText
                                fillColor: rowFill
                            }
                            DataCell {
                                width: resultList.width * 100 / root.tableWeight
                                value: model.azimuthText
                                textColor: rowText
                                fillColor: rowFill
                            }
                            DataCell {
                                width: resultList.width * 120 / root.tableWeight
                                value: model.bandText
                                textColor: Theme.bandColor(model.bandIndex)
                                fillColor: rowFill
                                bold: true
                            }
                            DataCell {
                                width: resultList.width * 170 / root.tableWeight
                                value: model.frequenciesText
                                textColor: rowText
                                fillColor: rowFill
                            }
                            DataCell {
                                width: resultList.width * 90 / root.tableWeight
                                value: model.qualityText
                                textColor: rowText
                                fillColor: rowFill
                            }
                            DataCell {
                                width: resultList.width * 110 / root.tableWeight
                                value: model.statusText
                                textColor: root.statusColor(model.statusText)
                                fillColor: rowFill
                                bold: true
                            }
                            DataCell {
                                width: resultList.width * 110 / root.tableWeight
                                value: model.pulseRepetitionPeriodText
                                textColor: rowText
                                fillColor: rowFill
                            }
                            DataCell {
                                width: resultList.width * 110 / root.tableWeight
                                value: model.pulseWidthText
                                textColor: rowText
                                fillColor: rowFill
                            }
                        }
                    }

                    Text {
                        anchors.centerIn: resultList
                        visible: ResultTableModel.count === 0
                        text: qsTr("Нет сохраненных результатов")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSmall
                    }
                }
            }
        }
    }

    component HeaderCell: Rectangle {
        property string title: ""
        property string toolTip: ""

        height: 32
        color: Theme.chipBackground
        border.color: Theme.divider
        border.width: 1

        ToolTip.visible: toolTip.length > 0 && headerHover.hovered
        ToolTip.text: toolTip
        ToolTip.delay: 600

        HoverHandler {
            id: headerHover
        }

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
