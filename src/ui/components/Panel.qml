import QtQuick
import SiriusScope 1.0

Rectangle {
    id: root

    property alias contentItem: content
    property int contentMargins: Theme.panelPadding

    radius: Theme.radiusPanel
    border.color: Theme.panelBorder
    border.width: 1
    clip: true
    gradient: Gradient {
        GradientStop { position: 0.0; color: Theme.panelTop }
        GradientStop { position: 1.0; color: Theme.panelBottom }
    }

    Item {
        id: content
        anchors.fill: parent
        anchors.margins: root.contentMargins
    }
}
