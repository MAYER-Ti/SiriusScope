import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import SiriusScope 1.0

Window {
    id: root

    property int bandId: 0
    property real centerHz: 0
    property real widthHz: 0
    property real thresholdAmplitude: 0
    property int inputAttenuatorDb: 0
    property int outputAttenuatorDb: 0
    property string polarization: "horizontal"
    property real globalMinHz: 0
    property real globalMaxHz: 0
    property real minAmplitude: 0
    property real maxAmplitude: 500

    property bool _updatingFields: false
    property real _thresholdDraft: thresholdAmplitude
    property bool _savedIndicatorActive: false

    signal saveRequested(int bandId, real centerHz, real widthHz, real thresholdAmplitude,
                         int inputAttenuatorDb, int outputAttenuatorDb, string polarization)
    signal thresholdPreviewChanged(int bandId, real thresholdAmplitude)
    signal canceled(int bandId)
    signal windowClosed(int bandId)

    title: qsTr("Настройка диапазона %1").arg(bandId + 1)
    width: 520
    height: 460
    minimumWidth: 480
    minimumHeight: 460
    modality: Qt.NonModal
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: Theme.panelBottom

    Component.onCompleted: {
        _thresholdDraft = clampAmplitude(thresholdAmplitude)
        thresholdField.text = _thresholdDraft.toFixed(0)
        setFrequenciesHz(centerHz - widthHz * 0.5, centerHz + widthHz * 0.5)
        inputAttenuator.currentIndex = indexForAttenuator(inputAttenuatorDb)
        outputAttenuator.currentIndex = indexForAttenuator(outputAttenuatorDb)
        polarizationMode.currentIndex = indexForPolarization(polarization)
        visible = true
    }

    onClosing: {
        canceled(bandId)
        windowClosed(bandId)
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.panelBottom
        border.color: Theme.panelBorder

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Rectangle {
                id: titleBar
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                color: Theme.chromeTopBar

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    anchors.right: closeButton.left
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Настройка диапазона %1").arg(root.bandId + 1)
                    color: Theme.textPrimary
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontMedium
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                MouseArea {
                    anchors.left: parent.left
                    anchors.right: closeButton.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom

                    property point pressPoint: Qt.point(0, 0)
                    property point windowPoint: Qt.point(0, 0)

                    onPressed: (mouse) => {
                        pressPoint = Qt.point(mouse.screenX, mouse.screenY)
                        windowPoint = Qt.point(root.x, root.y)
                        if (root.startSystemMove) {
                            root.startSystemMove()
                        }
                    }

                    onPositionChanged: (mouse) => {
                        if (root.startSystemMove) {
                            return
                        }
                        root.x = windowPoint.x + mouse.screenX - pressPoint.x
                        root.y = windowPoint.y + mouse.screenY - pressPoint.y
                    }
                }

                Rectangle {
                    id: closeButton
                    anchors.right: parent.right
                    anchors.top: parent.top
                    width: 46
                    height: parent.height
                    color: closeMouse.containsMouse ? Theme.statusBad : "transparent"

                    Text {
                        anchors.centerIn: parent
                        text: "\u00D7"
                        color: Theme.textPrimary
                        font.family: "Segoe UI, Arial, sans-serif"
                        font.pixelSize: Theme.fontLarge
                    }

                    MouseArea {
                        id: closeMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: root.close()
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 16
                spacing: 12

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 10
                    rowSpacing: 8

                    Label {
                        text: qsTr("Левая граница, МГц")
                        color: Theme.textSecondary
                        font.family: Theme.monoFontFamily
                        font.pixelSize: Theme.fontNormal
                    }

                    TextField {
                        id: leftFrequencyField
                        Layout.fillWidth: true
                        color: Theme.textPrimary
                        selectionColor: Theme.signalCyan
                        selectedTextColor: Theme.appBackgroundDeep
                        font.family: Theme.monoFontFamily
                        font.pixelSize: Theme.fontNormal
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        background: FieldBackground {}
                        onTextEdited: markDirtyAndValidate()
                    }

                    Label {
                        text: qsTr("Правая граница, МГц")
                        color: Theme.textSecondary
                        font.family: Theme.monoFontFamily
                        font.pixelSize: Theme.fontNormal
                    }

                    TextField {
                        id: rightFrequencyField
                        Layout.fillWidth: true
                        color: Theme.textPrimary
                        selectionColor: Theme.signalCyan
                        selectedTextColor: Theme.appBackgroundDeep
                        font.family: Theme.monoFontFamily
                        font.pixelSize: Theme.fontNormal
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        background: FieldBackground {}
                        onTextEdited: markDirtyAndValidate()
                    }

                    Label {
                        text: qsTr("Тип поляризации")
                        color: Theme.textSecondary
                        font.family: Theme.monoFontFamily
                        font.pixelSize: Theme.fontNormal
                    }

                    TextComboBox {
                        id: polarizationMode
                        Layout.fillWidth: true
                        model: [
                            { "value": "horizontal", "label": qsTr("Горизонтальная") },
                            { "value": "vertical", "label": qsTr("Вертикальная") }
                        ]
                        onActivated: root._savedIndicatorActive = false
                    }

                    Label {
                        text: qsTr("Амплитудный фильтр")
                        color: Theme.textSecondary
                        font.family: Theme.monoFontFamily
                        font.pixelSize: Theme.fontNormal
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Slider {
                            id: thresholdSlider
                            Layout.fillWidth: true
                            from: root.minAmplitude
                            to: root.maxAmplitude
                            value: root._thresholdDraft
                            onMoved: {
                                root._savedIndicatorActive = false
                                root._thresholdDraft = value
                                thresholdField.text = value.toFixed(0)
                                if (validateForm()) {
                                    thresholdPreviewChanged(root.bandId, root._thresholdDraft)
                                }
                            }
                        }

                        TextField {
                            id: thresholdField
                            Layout.preferredWidth: 78
                            color: Theme.textPrimary
                            selectionColor: Theme.signalCyan
                            selectedTextColor: Theme.appBackgroundDeep
                            font.family: Theme.monoFontFamily
                            font.pixelSize: Theme.fontNormal
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            background: FieldBackground {}
                            onEditingFinished: {
                                root._savedIndicatorActive = false
                                var parsed = parseNumber(text)
                                if (isNaN(parsed)) {
                                    parsed = root._thresholdDraft
                                }
                                root._thresholdDraft = clampAmplitude(parsed)
                                text = root._thresholdDraft.toFixed(0)
                                thresholdSlider.value = root._thresholdDraft
                                if (validateForm()) {
                                    thresholdPreviewChanged(root.bandId, root._thresholdDraft)
                                }
                            }
                            onTextEdited: markDirtyAndValidate()
                        }
                    }

                    Label {
                        text: qsTr("Входной аттенюатор")
                        color: Theme.textSecondary
                        font.family: Theme.monoFontFamily
                        font.pixelSize: Theme.fontNormal
                    }

                    ThemedComboBox {
                        id: inputAttenuator
                        Layout.fillWidth: true
                        model: [0, 10, 20, 30]
                        onActivated: root._savedIndicatorActive = false
                    }

                    Label {
                        text: qsTr("Выходной аттенюатор")
                        color: Theme.textSecondary
                        font.family: Theme.monoFontFamily
                        font.pixelSize: Theme.fontNormal
                    }

                    ThemedComboBox {
                        id: outputAttenuator
                        Layout.fillWidth: true
                        model: [0, 10, 20, 30]
                        onActivated: root._savedIndicatorActive = false
                    }
                }

                Text {
                    id: errorText
                    Layout.fillWidth: true
                    text: ""
                    color: Theme.statusBad
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                    visible: text.length > 0
                }

                Item {
                    Layout.fillHeight: true
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Item {
                        Layout.fillWidth: true
                    }

                    ThemedButton {
                        text: qsTr("Отмена")
                        onClicked: root.close()
                    }

                    ThemedButton {
                        id: saveButton
                        text: root._savedIndicatorActive ? qsTr("Сохранено") : qsTr("Сохранить")
                        savedState: root._savedIndicatorActive
                        enabled: errorText.text.length === 0
                        onClicked: {
                            if (!validateForm()) {
                                return
                            }
                            var leftHz = mhzToHz(parseNumber(leftFrequencyField.text))
                            var rightHz = mhzToHz(parseNumber(rightFrequencyField.text))
                            var nextWidthHz = rightHz - leftHz
                            var nextCenterHz = (leftHz + rightHz) * 0.5
                            saveRequested(root.bandId, nextCenterHz, nextWidthHz,
                                          root._thresholdDraft,
                                          Number(inputAttenuator.currentValue),
                                          Number(outputAttenuator.currentValue),
                                          currentPolarization())
                        }
                    }
                }
            }
        }
    }

    component FieldBackground: Rectangle {
        implicitHeight: 38
        radius: Theme.radiusInset
        color: Theme.insetBackground
        border.color: Theme.panelBorder
    }

    component ThemedButton: Button {
        id: control
        property bool savedState: false
        implicitWidth: 120
        implicitHeight: 38
        font.family: Theme.monoFontFamily
        font.pixelSize: Theme.fontNormal

        contentItem: Text {
            text: control.text
            color: control.enabled ? Theme.textPrimary : Theme.textVeryMuted
            font: control.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        background: Rectangle {
            radius: Theme.radiusInset
            color: !control.enabled ? Theme.insetBackground
                  : control.savedState ? Theme.statusGood
                  : control.down ? Theme.signalCyan
                  : control.hovered ? Theme.chipBackground
                  : Theme.insetBackground
            border.color: control.savedState ? Theme.statusGood
                         : control.hovered && control.enabled ? Theme.signalCyan
                         : Theme.panelBorder
        }
    }

    component ThemedComboBox: ComboBox {
        id: control
        implicitHeight: 38
        displayText: currentValue + " dB"
        font.family: Theme.monoFontFamily
        font.pixelSize: Theme.fontNormal

        contentItem: Text {
            leftPadding: 10
            rightPadding: control.indicator.width + 10
            text: control.displayText
            color: Theme.textPrimary
            font: control.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        indicator: Text {
            x: control.width - width - 10
            y: (control.height - height) * 0.5
            text: "\u25BE"
            color: Theme.textSecondary
            font.pixelSize: Theme.fontNormal
        }

        background: Rectangle {
            radius: Theme.radiusInset
            color: control.down || control.popup.visible ? Theme.chipBackground : Theme.insetBackground
            border.color: control.hovered || control.popup.visible ? Theme.signalCyan : Theme.panelBorder
        }

        delegate: ItemDelegate {
            width: control.width
            height: 38
            text: modelData + " dB"
            highlighted: control.highlightedIndex === index

            contentItem: Text {
                text: parent.text
                color: parent.highlighted ? Theme.textPrimary : Theme.textSecondary
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontNormal
                verticalAlignment: Text.AlignVCenter
                leftPadding: 10
            }

            background: Rectangle {
                color: parent.highlighted ? Theme.chipBackground : Theme.panelBottom
            }
        }

        popup: Popup {
            y: control.height + 2
            width: control.width
            implicitHeight: contentItem.implicitHeight
            padding: 1

            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: control.popup.visible ? control.delegateModel : null
                currentIndex: control.highlightedIndex
            }

            background: Rectangle {
                color: Theme.panelBottom
                border.color: Theme.panelBorder
                radius: Theme.radiusInset
            }
        }
    }

    component TextComboBox: ComboBox {
        id: control
        implicitHeight: 38
        font.family: Theme.monoFontFamily
        font.pixelSize: Theme.fontNormal

        displayText: {
            if (!model || currentIndex < 0 || currentIndex >= model.length) {
                return ""
            }
            return model[currentIndex].label
        }

        contentItem: Text {
            leftPadding: 10
            rightPadding: control.indicator.width + 10
            text: control.displayText
            color: Theme.textPrimary
            font: control.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        indicator: Text {
            x: control.width - width - 10
            y: (control.height - height) * 0.5
            text: "\u25BE"
            color: Theme.textSecondary
            font.pixelSize: Theme.fontNormal
        }

        background: Rectangle {
            radius: Theme.radiusInset
            color: control.down || control.popup.visible ? Theme.chipBackground : Theme.insetBackground
            border.color: control.hovered || control.popup.visible ? Theme.signalCyan : Theme.panelBorder
        }

        delegate: ItemDelegate {
            width: control.width
            height: 38
            highlighted: control.highlightedIndex === index

            contentItem: Text {
                text: modelData.label
                color: parent.highlighted ? Theme.textPrimary : Theme.textSecondary
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontNormal
                verticalAlignment: Text.AlignVCenter
                leftPadding: 10
            }

            background: Rectangle {
                color: parent.highlighted ? Theme.chipBackground : Theme.panelBottom
            }
        }

        popup: Popup {
            y: control.height + 2
            width: control.width
            implicitHeight: contentItem.implicitHeight
            padding: 1

            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: control.popup.visible ? control.delegateModel : null
                currentIndex: control.highlightedIndex
            }

            background: Rectangle {
                color: Theme.panelBottom
                border.color: Theme.panelBorder
                radius: Theme.radiusInset
            }
        }
    }

    function setFrequenciesHz(leftHz, rightHz) {
        _updatingFields = true
        leftFrequencyField.text = hzToMhz(leftHz).toFixed(3)
        rightFrequencyField.text = hzToMhz(rightHz).toFixed(3)
        _updatingFields = false
        validateForm()
    }

    function validateForm() {
        if (_updatingFields) {
            return true
        }

        var leftMhz = parseNumber(leftFrequencyField.text)
        var rightMhz = parseNumber(rightFrequencyField.text)
        var threshold = parseNumber(thresholdField.text)

        if (isNaN(leftMhz) || isNaN(rightMhz) || isNaN(threshold)) {
            errorText.text = qsTr("Введите числовые значения.")
            return false
        }

        var leftHz = mhzToHz(leftMhz)
        var rightHz = mhzToHz(rightMhz)
        var widthHz = rightHz - leftHz

        if (leftHz < globalMinHz || rightHz > globalMaxHz) {
            errorText.text = qsTr("Границы полосы должны быть в пределах спектра.")
            return false
        }

        if (rightHz <= leftHz || widthHz <= 0 || widthHz > 500e6) {
            errorText.text = qsTr("Ширина полосы должна быть больше 0 и не больше 500 МГц.")
            return false
        }

        if (threshold < minAmplitude || threshold > maxAmplitude) {
            errorText.text = qsTr("Амплитудный фильтр должен быть в допустимом диапазоне.")
            return false
        }

        if (polarizationMode.currentIndex < 0) {
            errorText.text = qsTr("Выберите тип поляризации.")
            return false
        }

        root._thresholdDraft = clampAmplitude(threshold)
        thresholdSlider.value = root._thresholdDraft
        errorText.text = ""
        return true
    }

    function markDirtyAndValidate() {
        _savedIndicatorActive = false
        validateForm()
    }

    function updateDraftFrequenciesHz(centerHz, widthHz) {
        _savedIndicatorActive = false
        setFrequenciesHz(centerHz - widthHz * 0.5, centerHz + widthHz * 0.5)
    }

    function markSettingsSaved() {
        errorText.text = ""
        _savedIndicatorActive = true
    }

    function showControllerError(message) {
        _savedIndicatorActive = false
        errorText.text = message
    }

    function parseNumber(text) {
        return Number(String(text).replace(",", "."))
    }

    function hzToMhz(valueHz) {
        return valueHz / 1e6
    }

    function mhzToHz(valueMhz) {
        return valueMhz * 1e6
    }

    function clampAmplitude(value) {
        return Math.max(minAmplitude, Math.min(maxAmplitude, value))
    }

    function indexForAttenuator(valueDb) {
        var values = [0, 10, 20, 30]
        for (var i = 0; i < values.length; i++) {
            if (values[i] === valueDb) {
                return i
            }
        }
        return 0
    }

    function currentPolarization() {
        if (!polarizationMode.model || polarizationMode.currentIndex < 0
                || polarizationMode.currentIndex >= polarizationMode.model.length) {
            return "horizontal"
        }
        return polarizationMode.model[polarizationMode.currentIndex].value
    }

    function indexForPolarization(value) {
        if (!polarizationMode.model) {
            return 0
        }
        var normalized = String(value).toLowerCase()
        for (var i = 0; i < polarizationMode.model.length; i++) {
            if (polarizationMode.model[i].value === normalized) {
                return i
            }
        }
        return 0
    }
}
