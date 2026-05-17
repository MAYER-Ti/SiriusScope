import QtQuick
import SiriusScope 1.0

Rectangle {
    id: root

    property bool scanActive: false
    property real progress: 0
    property string stateText: ""

    readonly property real clampedProgress: Math.max(0, Math.min(1, progress))

    implicitHeight: 30
    radius: Theme.radiusInset
    color: Theme.chipBackground
    border.color: Theme.panelBorder
    clip: true

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: root.width * root.clampedProgress
        radius: Theme.radiusInset
        color: Theme.signalCyan
        opacity: root.scanActive || root.clampedProgress > 0 ? 0.82 : 0.0

        Behavior on width {
            NumberAnimation { duration: 120 }
        }
    }

    Text {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        text: root.scanActive
              ? Math.round(root.clampedProgress * 100).toString() + "%"
              : root.stateText
        color: Theme.textSecondary
        font.family: Theme.monoFontFamily
        font.pixelSize: Theme.fontSmall
        horizontalAlignment: Text.AlignCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
