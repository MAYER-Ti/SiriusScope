import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SiriusScope 1.0

Rectangle {
    id: root

    implicitHeight: 40
    radius: Theme.radiusPanel
    color: Theme.panelBottom
    border.color: Theme.panelBorder

    function setMode(mode) {
        AppState.mode = mode
    }

    function modeActive(mode) {
        return AppState.mode === mode
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 12
        spacing: 10

        Rectangle {
            Layout.preferredWidth: 232
            Layout.fillHeight: true
            Layout.topMargin: 9
            Layout.bottomMargin: 9
            radius: Theme.radiusInset
            color: Theme.insetBackground
            border.color: Theme.panelBorder

            RowLayout {
                anchors.fill: parent
                anchors.margins: 2
                spacing: 0

                Repeater {
                    model: [
                        { label: qsTr("Тест"), mode: AppState.Test },
                        { label: qsTr("Боевой"), mode: AppState.Combat },
                        { label: qsTr("Контроль"), mode: AppState.Control }
                    ]

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 3
                        color: root.modeActive(modelData.mode) ? "#214B35" : "transparent"
                        border.color: root.modeActive(modelData.mode) ? "#2E7650" : "transparent"

                        Text {
                            anchors.centerIn: parent
                            text: modelData.label
                            color: root.modeActive(modelData.mode) ? "#DDF8E9" : Theme.textLabel
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.setMode(modelData.mode)
                        }
                    }
                }
            }
        }

        StatusChip {
            Layout.preferredWidth: 130
            label: ""
            value: AppState.mode === AppState.Test ? "SIMULATOR" : "HARDWARE"
            statusColor: Theme.statusGood
        }

        StatusChip {
            Layout.preferredWidth: 118
            label: ""
            value: "BCO UDP"
            statusColor: Theme.statusGood
        }

        StatusChip {
            Layout.preferredWidth: 142
            label: ""
            value: "ANT TCP"
            statusColor: Theme.statusGood
        }

        StatusChip {
            Layout.preferredWidth: 124
            label: ""
            value: "REC 00:18"
            statusColor: Theme.statusWarn
        }

        Item {
            Layout.fillWidth: true
            Layout.minimumWidth: 20
        }

        Text {
            Layout.alignment: Qt.AlignVCenter
            text: qsTr("Диапазон наблюдения")
            color: Theme.textLabel
            font.pixelSize: 11
            elide: Text.ElideRight
            visible: root.width > 980
        }

        Rectangle {
            Layout.preferredWidth: 160
            Layout.preferredHeight: 22
            radius: Theme.radiusInset
            color: Theme.insetBackground
            border.color: Theme.panelBorder

            Text {
                anchors.centerIn: parent
                text: (FrequencyViewportModel.viewMinHz / 1e9).toFixed(2) + "-" +
                      (FrequencyViewportModel.viewMaxHz / 1e9).toFixed(2) + " GHz"
                color: Theme.textPrimary
                font.family: Theme.monoFontFamily
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }
        }

        Button {
            Layout.preferredWidth: 62
            Layout.preferredHeight: 26
            text: qsTr("Старт")
            font.pixelSize: 11
            font.family: Theme.monoFontFamily
            contentItem: Text {
                text: parent.text
                color: Theme.textPrimary
                font: parent.font
                horizontalAlignment: Text.AlignCenter
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            background: Rectangle {
                radius: 5
                color: "#183B2A"
                border.color: Theme.statusGood
            }
        }

        Button {
            Layout.preferredWidth: 58
            Layout.preferredHeight: 26
            text: qsTr("Стоп")
            font.pixelSize: 11
            font.family: Theme.monoFontFamily
            contentItem: Text {
                text: parent.text
                color: Theme.textPrimary
                font: parent.font
                horizontalAlignment: Text.AlignCenter
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            background: Rectangle {
                radius: 5
                color: "#3A1D20"
                border.color: Theme.statusBad
            }
        }
    }
}
