import QtQuick
import QtQuick.Controls
import SiriusScope 1.0

Item {
    id: root

    readonly property string monoFontFamily: "Consolas, Monospace"

    property int bandId: 0
    property real centerHz: 0
    property real widthHz: 0
    property real thresholdAmplitude: 180
    property bool enabled: true
    property real viewMinHz: 0
    property real viewMaxHz: 0
    property real globalMinHz: 0
    property real globalMaxHz: 0
    property real minWidthHz: 100e6
    property real maxWidthHz: 500e6
    property real minAmplitude: 0
    property real maxAmplitude: 500
    property bool panModifierActive: false

    signal bandEdited(real centerHz, real widthHz, bool isFinal)
    signal thresholdEdited(real thresholdAmplitude, bool isFinal)
    signal enabledEdited(bool enabled, bool isFinal)

    readonly property real viewSpanHz: Math.max(1.0, viewMaxHz - viewMinHz)
    readonly property real bandMinHz: centerHz - widthHz * 0.5
    readonly property real bandMaxHz: centerHz + widthHz * 0.5
    readonly property real visibleMinHz: Math.max(viewMinHz, bandMinHz)
    readonly property real visibleMaxHz: Math.min(viewMaxHz, bandMaxHz)

    property real _pendingCenterHz: centerHz
    property real _pendingWidthHz: widthHz
    property real _pendingThresholdAmplitude: thresholdAmplitude
    property point _lastRootPoint: Qt.point(0, 0)

    anchors.fill: parent
    visible: visibleMaxHz > visibleMinHz

    Rectangle {
        id: bandRect
        x: (visibleMinHz - viewMinHz) / viewSpanHz * parent.width
        width: Math.max(1, (visibleMaxHz - visibleMinHz) / viewSpanHz * parent.width)
        height: parent.height
        z: 2
        color: enabled ? "#3353b0ff" : "#1f444b55"
        border.color: enabled ? "#7fb3ff" : "#5a6a74"
        border.width: 1

        Rectangle {
            id: leftHandle
            width: 8
            height: parent.height
            color: enabled ? "#7fb3ff" : "#5a6a74"
        }

        Rectangle {
            id: rightHandle
            width: 8
            height: parent.height
            anchors.right: parent.right
            color: enabled ? "#7fb3ff" : "#5a6a74"
        }

        Text {
            id: bandLabel
            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.top: parent.top
            anchors.topMargin: 4
            text: "B" + (bandId + 1) + " " + thresholdAmplitude.toFixed(0)
            color: enabled ? "#e7eef8" : "#a0a6ad"
            font.family: root.monoFontFamily
            font.pixelSize: 10
        }

        MouseArea {
            id: bodyDrag
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            hoverEnabled: true
            enabled: !leftResize.containsMouse && !rightResize.containsMouse

            property bool dragging: false
            property real startRootX: 0
            property real startCenterHz: 0

            onPressed: (mouse) => {
                if (mouse.button === Qt.LeftButton && panModifierActive) {
                    mouse.accepted = false
                    return
                }
                if (mouse.button === Qt.RightButton) {
                    openThresholdPopupAt(bodyDrag, mouse.x, mouse.y)
                    return
                }
                dragging = true
                _lastRootPoint = root.mapFromItem(bodyDrag, mouse.x, mouse.y)
                startRootX = _lastRootPoint.x
                startCenterHz = centerHz
            }

            onPositionChanged: (mouse) => {
                if (!dragging) {
                    return
                }
                _lastRootPoint = root.mapFromItem(bodyDrag, mouse.x, mouse.y)
                var deltaHz = (_lastRootPoint.x - startRootX) / root.width * viewSpanHz
                var nextCenter = clampCenter(startCenterHz + deltaHz, widthHz)
                scheduleBandChange(nextCenter, widthHz, false)
            }

            onReleased: (mouse) => {
                if (!dragging) {
                    return
                }
                dragging = false
                _lastRootPoint = root.mapFromItem(bodyDrag, mouse.x, mouse.y)
                var deltaHz = (_lastRootPoint.x - startRootX) / root.width * viewSpanHz
                var nextCenter = clampCenter(startCenterHz + deltaHz, widthHz)
                scheduleBandChange(nextCenter, widthHz, true)
            }
        }

        MouseArea {
            id: leftResize
            width: leftHandle.width
            height: parent.height
            anchors.left: parent.left
            acceptedButtons: Qt.LeftButton
            hoverEnabled: true
            cursorShape: Qt.SizeHorCursor
            z: 3

            property bool resizing: false
            property real startRootX: 0
            property real startMinHz: 0
            property real startMaxHz: 0

            onPressed: (mouse) => {
                if (panModifierActive) {
                    mouse.accepted = false
                    return
                }
                resizing = true
                _lastRootPoint = root.mapFromItem(leftResize, mouse.x, mouse.y)
                startRootX = _lastRootPoint.x
                startMinHz = bandMinHz
                startMaxHz = bandMaxHz
            }

            onPositionChanged: (mouse) => {
                if (!resizing) {
                    return
                }
                _lastRootPoint = root.mapFromItem(leftResize, mouse.x, mouse.y)
                var deltaHz = (_lastRootPoint.x - startRootX) / root.width * viewSpanHz
                var nextMin = startMinHz + deltaHz
                var result = clampEdges(nextMin, startMaxHz)
                scheduleBandChange(result.centerHz, result.widthHz, false)
            }

            onReleased: (mouse) => {
                if (!resizing) {
                    return
                }
                resizing = false
                _lastRootPoint = root.mapFromItem(leftResize, mouse.x, mouse.y)
                var deltaHz = (_lastRootPoint.x - startRootX) / root.width * viewSpanHz
                var nextMin = startMinHz + deltaHz
                var result = clampEdges(nextMin, startMaxHz)
                scheduleBandChange(result.centerHz, result.widthHz, true)
            }
        }

        MouseArea {
            id: rightResize
            width: rightHandle.width
            height: parent.height
            anchors.right: parent.right
            acceptedButtons: Qt.LeftButton
            hoverEnabled: true
            cursorShape: Qt.SizeHorCursor
            z: 3

            property bool resizing: false
            property real startRootX: 0
            property real startMinHz: 0
            property real startMaxHz: 0

            onPressed: (mouse) => {
                if (panModifierActive) {
                    mouse.accepted = false
                    return
                }
                resizing = true
                _lastRootPoint = root.mapFromItem(rightResize, mouse.x, mouse.y)
                startRootX = _lastRootPoint.x
                startMinHz = bandMinHz
                startMaxHz = bandMaxHz
            }

            onPositionChanged: (mouse) => {
                if (!resizing) {
                    return
                }
                _lastRootPoint = root.mapFromItem(rightResize, mouse.x, mouse.y)
                var deltaHz = (_lastRootPoint.x - startRootX) / root.width * viewSpanHz
                var nextMax = startMaxHz + deltaHz
                var result = clampEdges(startMinHz, nextMax)
                scheduleBandChange(result.centerHz, result.widthHz, false)
            }

            onReleased: (mouse) => {
                if (!resizing) {
                    return
                }
                resizing = false
                _lastRootPoint = root.mapFromItem(rightResize, mouse.x, mouse.y)
                var deltaHz = (_lastRootPoint.x - startRootX) / root.width * viewSpanHz
                var nextMax = startMaxHz + deltaHz
                var result = clampEdges(startMinHz, nextMax)
                scheduleBandChange(result.centerHz, result.widthHz, true)
            }
        }
    }

    Timer {
        id: bandPreviewTimer
        interval: 80
        repeat: false
        onTriggered: {
            SpectrumController.setBand(bandId, _pendingCenterHz, _pendingWidthHz, false)
        }
    }

    Timer {
        id: thresholdPreviewTimer
        interval: 80
        repeat: false
        onTriggered: {
            SpectrumController.setBandThreshold(bandId, _pendingThresholdAmplitude, false)
        }
    }

    Popup {
        id: thresholdPopup
        modal: false
        focus: true
        width: 200
        height: 120
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: "#1b2028"
            border.color: "#4c5868"
            radius: 4
        }

        Column {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 8

            Text {
                text: "Amplitude threshold"
                color: "#d7dbe2"
                font.family: root.monoFontFamily
                font.pixelSize: 10
            }

            Slider {
                id: thresholdSlider
                from: minAmplitude
                to: maxAmplitude
                value: thresholdAmplitude
                onMoved: {
                    scheduleThresholdChange(value, false)
                }
                onPressedChanged: {
                    if (!pressed) {
                        scheduleThresholdChange(value, true)
                    }
                }
            }

            Row {
                spacing: 6
                CheckBox {
                    id: enabledCheck
                    checked: enabled
                    text: "Enabled"
                    onCheckedChanged: {
                        enabledEdited(checked, true)
                        SpectrumController.setBandEnabled(bandId, checked)
                    }
                }
                Text {
                    text: thresholdAmplitude.toFixed(0)
                    color: "#a9b2bd"
                    font.family: root.monoFontFamily
                    font.pixelSize: 10
                }
            }
        }
    }

    function scheduleBandChange(nextCenter, nextWidth, isFinal) {
        _pendingCenterHz = nextCenter
        _pendingWidthHz = nextWidth
        bandEdited(nextCenter, nextWidth, isFinal)
        if (isFinal) {
            SpectrumController.setBand(bandId, nextCenter, nextWidth, true)
        } else {
            bandPreviewTimer.restart()
        }
    }

    function scheduleThresholdChange(nextThresholdAmplitude, isFinal) {
        _pendingThresholdAmplitude = nextThresholdAmplitude
        thresholdEdited(nextThresholdAmplitude, isFinal)
        if (isFinal) {
            SpectrumController.setBandThreshold(bandId, nextThresholdAmplitude, true)
        } else {
            thresholdPreviewTimer.restart()
        }
    }

    function clampedPopupCoordinate(value, popupSize, availableSize) {
        if (availableSize <= popupSize) {
            return 0
        }
        return Math.max(0, Math.min(value, availableSize - popupSize))
    }

    function openThresholdPopupAt(sourceItem, sourceX, sourceY) {
        var clickPoint = root.mapFromItem(sourceItem, sourceX, sourceY)
        thresholdPopup.x = clampedPopupCoordinate(clickPoint.x + 8, thresholdPopup.width, root.width)
        thresholdPopup.y = clampedPopupCoordinate(clickPoint.y + 8, thresholdPopup.height, root.height)
        thresholdPopup.open()
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

    function clampEdges(nextMin, nextMax) {
        var minHz = Math.min(nextMin, nextMax)
        var maxHz = Math.max(nextMin, nextMax)

        var width = maxHz - minHz
        width = Math.max(minWidthHz, Math.min(maxWidthHz, width))

        maxHz = minHz + width
        if (minHz < globalMinHz) {
            minHz = globalMinHz
            maxHz = minHz + width
        }
        if (maxHz > globalMaxHz) {
            maxHz = globalMaxHz
            minHz = maxHz - width
        }

        return {
            centerHz: (minHz + maxHz) * 0.5,
            widthHz: width
        }
    }
}
