import QtQuick
import QtQuick.Controls
import SiriusScope 1.0

Item {
    id: root

    readonly property string monoFontFamily: Theme.monoFontFamily

    property int bandId: 0
    property real centerHz: 0
    property real widthHz: 0
    property real thresholdAmplitude: 30
    property color bandColor: Theme.bandColor(bandId)
    property color bandBorderColor: Theme.bandBorderColor(bandId)
    property color bandTextColor: Theme.bandTextColor(bandId)
    property real viewMinHz: 0
    property real viewMaxHz: 0
    property real globalMinHz: 0
    property real globalMaxHz: 0
    property bool settingsWindowOpen: false
    property bool panModifierActive: false
    property bool editingLocked: false
    property bool dragging: false
    property real previewCenterHz: centerHz

    signal configureRequested(int bandId)
    signal bandPreviewMoved(int bandId, real centerHz, real widthHz)
    signal bandPreviewFinished(int bandId, real centerHz, real widthHz)

    readonly property real viewSpanHz: Math.max(1.0, viewMaxHz - viewMinHz)
    readonly property real effectiveCenterHz: dragging ? previewCenterHz : centerHz
    readonly property real bandMinHz: effectiveCenterHz - widthHz * 0.5
    readonly property real bandMaxHz: effectiveCenterHz + widthHz * 0.5
    readonly property real visibleMinHz: Math.max(viewMinHz, bandMinHz)
    readonly property real visibleMaxHz: Math.min(viewMaxHz, bandMaxHz)

    anchors.fill: parent
    visible: visibleMaxHz > visibleMinHz

    onCenterHzChanged: {
        if (!dragging) {
            previewCenterHz = centerHz
        }
    }
    onEditingLockedChanged: {
        if (editingLocked) {
            dragging = false
            previewCenterHz = centerHz
        }
    }

    Rectangle {
        id: bandRect
        x: (visibleMinHz - viewMinHz) / viewSpanHz * parent.width
        width: Math.max(1, (visibleMaxHz - visibleMinHz) / viewSpanHz * parent.width)
        height: parent.height
        z: 2
        color: root.bandColor
        opacity: root.settingsWindowOpen ? 0.24 : 0.18
        border.color: root.bandBorderColor
        border.width: root.settingsWindowOpen ? 2 : 1

        Rectangle {
            id: leftEdge
            width: 3
            height: parent.height
            color: root.bandBorderColor
            opacity: 0.9
        }

        Rectangle {
            id: rightEdge
            width: 3
            height: parent.height
            anchors.right: parent.right
            color: root.bandBorderColor
            opacity: 0.9
        }

        Text {
            id: bandLabel
            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.top: parent.top
            anchors.topMargin: 4
            width: Math.max(0, parent.width - 20)
            text: qsTr("Диапазон %1").arg(bandId + 1) + " " + thresholdAmplitude.toFixed(0)
            color: root.bandTextColor
            font.family: root.monoFontFamily
            font.pixelSize: Theme.fontSmall
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        MouseArea {
            id: bodyDrag
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            hoverEnabled: true

            property real startRootX: 0
            property real startCenterHz: 0

            onPressed: (mouse) => {
                if (mouse.button === Qt.RightButton) {
                    contextMenu.x = mouse.x
                    contextMenu.y = mouse.y
                    contextMenu.open()
                    return
                }

                if (mouse.button !== Qt.LeftButton || panModifierActive || !settingsWindowOpen
                        || editingLocked) {
                    root.dragging = false
                    mouse.accepted = false
                    return
                }

                root.previewCenterHz = centerHz
                root.dragging = true
                var rootPoint = root.mapFromItem(bodyDrag, mouse.x, mouse.y)
                startRootX = rootPoint.x
                startCenterHz = root.effectiveCenterHz
            }

            onPositionChanged: (mouse) => {
                if (!root.dragging) {
                    return
                }
                var rootPoint = root.mapFromItem(bodyDrag, mouse.x, mouse.y)
                var deltaHz = (rootPoint.x - startRootX) / root.width * viewSpanHz
                var nextCenter = clampCenter(startCenterHz + deltaHz, widthHz)
                root.previewCenterHz = nextCenter
                bandPreviewMoved(bandId, nextCenter, widthHz)
            }

            onReleased: (mouse) => {
                if (!root.dragging) {
                    return
                }
                var rootPoint = root.mapFromItem(bodyDrag, mouse.x, mouse.y)
                var deltaHz = (rootPoint.x - startRootX) / root.width * viewSpanHz
                var nextCenter = clampCenter(startCenterHz + deltaHz, widthHz)
                root.previewCenterHz = nextCenter
                bandPreviewFinished(bandId, nextCenter, widthHz)
                root.dragging = false
            }
        }

        Popup {
            id: contextMenu
            width: 160
            height: 42
            padding: 2
            modal: false
            focus: true
            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

            background: Rectangle {
                color: Theme.panelBottom
                border.color: Theme.panelBorder
                radius: Theme.radiusInset
            }

            contentItem: Rectangle {
                color: configureMouse.containsMouse ? Theme.chipBackground : Theme.panelBottom
                radius: Theme.radiusInset

                Text {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    text: qsTr("Настроить")
                    color: Theme.textPrimary
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontNormal
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }

                MouseArea {
                    id: configureMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        contextMenu.close()
                        root.configureRequested(root.bandId)
                    }
                }
            }
        }
    }

    function clampCenter(nextCenter, currentWidth) {
        var half = currentWidth * 0.5
        var minCenter = globalMinHz + half
        var maxCenter = globalMaxHz - half
        if (minCenter > maxCenter) {
            return (globalMinHz + globalMaxHz) * 0.5
        }
        return Math.min(maxCenter, Math.max(minCenter, nextCenter))
    }
}
