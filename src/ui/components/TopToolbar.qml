import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SiriusScope 1.0

Rectangle {
    id: root

    property bool canScanSector: false

    signal liveRequested()
    signal recordingStartRequested()
    signal recordingStopRequested()
    signal scanSectorRequested()

    implicitHeight: 52
    radius: Theme.radiusPanel
    color: Theme.panelBottom
    border.color: Theme.panelBorder

    function setMode(mode) {
        AppState.mode = mode
    }

    function modeActive(mode) {
        return AppState.mode === mode
    }

    component ToolbarButton: Button {
        id: control

        property color normalColor: Theme.chipBackground
        property color hoveredColor: "#203040"
        property color pressedColor: "#24455E"
        property color disabledColor: "#121922"
        property color normalBorderColor: Theme.panelBorder
        property color accentBorderColor: Theme.signalCyan

        font.pixelSize: Theme.fontNormal
        font.family: Theme.monoFontFamily
        hoverEnabled: true

        contentItem: Text {
            text: control.text
            color: control.enabled ? Theme.textPrimary : Theme.textVeryMuted
            font: control.font
            horizontalAlignment: Text.AlignCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            radius: Theme.radiusInset
            color: !control.enabled
                   ? control.disabledColor
                   : control.down
                     ? control.pressedColor
                     : control.hovered
                       ? control.hoveredColor
                       : control.normalColor
            border.color: control.enabled && (control.down || control.hovered)
                          ? control.accentBorderColor
                          : control.normalBorderColor
            opacity: control.enabled ? 1.0 : 0.58
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        anchors.topMargin: 8
        anchors.bottomMargin: 8
        spacing: 12

        Text {
            Layout.alignment: Qt.AlignVCenter
            text: qsTr("Режим")
            color: Theme.textLabel
            font.pixelSize: Theme.fontNormal
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        Rectangle {
            Layout.preferredWidth: 340
            Layout.minimumWidth: 260
            Layout.fillHeight: true
            radius: Theme.radiusInset
            color: Theme.insetBackground
            border.color: Theme.panelBorder

            RowLayout {
                anchors.fill: parent
                anchors.margins: 3
                spacing: 3

                Repeater {
                    model: [
                        { label: qsTr("Генератор"), mode: AppState.Test },
                        { label: qsTr("Аппаратура"), mode: AppState.Combat },
                        { label: qsTr("Контроль"), mode: AppState.Control }
                    ]

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: Theme.radiusInset
                        color: root.modeActive(modelData.mode) ? "#214B35" : "transparent"
                        border.color: root.modeActive(modelData.mode) ? "#2E7650" : "transparent"

                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            text: modelData.label
                            color: root.modeActive(modelData.mode) ? "#DDF8E9" : Theme.textLabel
                            font.pixelSize: Theme.fontSmall
                            font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignCenter
                            verticalAlignment: Text.AlignVCenter
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

        Item {
            Layout.fillWidth: true
            Layout.minimumWidth: 12
        }

        ToolbarButton {
            Layout.preferredWidth: 176
            Layout.minimumWidth: 136
            Layout.fillHeight: true
            enabled: WaterfallController.sessionActive
            text: qsTr("К текущим данным")
            normalColor: "#183B2A"
            accentBorderColor: Theme.statusGood
            onClicked: root.liveRequested()
        }

        ToolbarButton {
            Layout.preferredWidth: 158
            Layout.minimumWidth: 118
            Layout.fillHeight: true
            enabled: !WaterfallController.sessionActive
            text: qsTr("Включить запись")
            normalColor: "#183B2A"
            accentBorderColor: Theme.statusGood
            onClicked: root.recordingStartRequested()
        }

        ToolbarButton {
            Layout.preferredWidth: 166
            Layout.minimumWidth: 122
            Layout.fillHeight: true
            enabled: WaterfallController.sessionActive
            text: qsTr("Выключить запись")
            normalColor: "#3B2418"
            accentBorderColor: Theme.statusWarn
            onClicked: root.recordingStopRequested()
        }

        ToolbarButton {
            Layout.preferredWidth: 196
            Layout.minimumWidth: 150
            Layout.fillHeight: true
            enabled: root.canScanSector
            text: qsTr("Сканировать сектор")
            normalColor: "#123044"
            accentBorderColor: Theme.signalCyan
            onClicked: root.scanSectorRequested()
        }
    }
}
