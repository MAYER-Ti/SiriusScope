import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SiriusScope 1.0

Rectangle {
    id: root

    property real value: 10
    property real from: 1
    property real to: 60
    property real stepSize: 1

    signal valueChangeRequested(real value)

    implicitHeight: 30
    radius: Theme.radiusInset
    color: Theme.chipBackground
    border.color: Theme.panelBorder
    opacity: enabled ? 1.0 : 0.58

    function requestValue(nextValue) {
        const clamped = Math.max(root.from, Math.min(root.to, Math.round(nextValue)))
        if (Math.abs(clamped - root.value) > 0.001) {
            root.valueChangeRequested(clamped)
        }
    }

    component StepButton: Button {
        id: control

        implicitWidth: 28
        implicitHeight: 24
        hoverEnabled: true
        font.family: Theme.monoFontFamily
        font.pixelSize: Theme.fontSmall

        contentItem: Text {
            text: control.text
            color: control.enabled ? Theme.textPrimary : Theme.textVeryMuted
            font: control.font
            horizontalAlignment: Text.AlignCenter
            verticalAlignment: Text.AlignVCenter
        }

        background: Rectangle {
            radius: Theme.radiusInset
            color: !control.enabled
                   ? "#121922"
                   : control.down
                     ? "#24455E"
                     : control.hovered
                       ? "#203040"
                       : Theme.insetBackground
            border.color: control.enabled && (control.down || control.hovered)
                          ? Theme.signalCyan
                          : Theme.panelBorderSoft
            opacity: control.enabled ? 1.0 : 0.62
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 3
        spacing: 6

        Text {
            Layout.preferredWidth: 76
            Layout.fillHeight: true
            text: qsTr("Скорость")
            color: Theme.textVeryMuted
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontSmall
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        StepButton {
            text: "−"
            enabled: root.enabled && root.value > root.from
            onClicked: root.requestValue(root.value - root.stepSize)
        }

        Text {
            Layout.preferredWidth: 56
            Layout.fillHeight: true
            text: Math.round(root.value).toString() + "°/с"
            color: root.enabled ? Theme.textPrimary : Theme.textVeryMuted
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontSmall
            horizontalAlignment: Text.AlignCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        StepButton {
            text: "+"
            enabled: root.enabled && root.value < root.to
            onClicked: root.requestValue(root.value + root.stepSize)
        }
    }
}
