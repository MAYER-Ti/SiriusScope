import QtQuick
import SiriusScope 1.0

Rectangle {
    id: root

    property string label: ""
    property string value: ""
    property color statusColor: Theme.statusGood
    property int fontSize: Theme.fontSmall
    property int horizontalPadding: 12

    implicitWidth: Math.max(96, (label.length + value.length + 2) * fontSize * 0.62 + horizontalPadding * 2 + 18)
    implicitHeight: 30
    radius: Theme.radiusInset
    color: Theme.chipBackground
    border.color: Theme.panelBorder

    Rectangle {
        id: dot
        width: 9
        height: 9
        radius: 4.5
        color: root.statusColor
        anchors.left: parent.left
        anchors.leftMargin: root.horizontalPadding
        anchors.verticalCenter: parent.verticalCenter
    }

    Text {
        anchors.left: dot.right
        anchors.leftMargin: 7
        anchors.right: parent.right
        anchors.rightMargin: root.horizontalPadding
        anchors.verticalCenter: parent.verticalCenter
        text: root.label.length > 0 ? root.label + ": " + root.value : root.value
        color: Theme.textSecondary
        font.family: Theme.monoFontFamily
        font.pixelSize: root.fontSize
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }
}
