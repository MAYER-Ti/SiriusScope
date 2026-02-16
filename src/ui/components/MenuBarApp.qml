import QtQuick
import QtQuick.Controls
import SiriusScope 1.0

MenuBar {
    id: menuBar

    delegate: MenuBarItem {
        implicitHeight: 22
        padding: 2
        font.pixelSize: 11
    }

    ActionGroup {
        id: appModeGroup
    }

    Menu {
        title: qsTr("Режим")
        Action {
            text: qsTr("Тестовый")
            checkable: true
            ActionGroup.group: appModeGroup
            checked: AppState.mode === AppState.Test
            onTriggered: {
                AppState.mode = AppState.Test
                console.log("Режим->Тестовый")
            }
        }
        Action {
            text: qsTr("Боевой")
            checkable: true
            ActionGroup.group: appModeGroup
            checked: AppState.mode === AppState.Combat
            onTriggered: {
                AppState.mode = AppState.Combat
            }
        }
        Action {
            text: qsTr("Контроль")
            checkable: true
            ActionGroup.group: appModeGroup
            checked: AppState.mode === AppState.Control
            onTriggered: {
                AppState.mode = AppState.Control
            }
        }
    }
    Menu {
        title: qsTr("Файл")
        Action {
            text: qsTr("Открыть")
            shortcut: StandardKey.Open
            onTriggered: console.log("Файл->Открыть")
        }
        Action {
            text: qsTr("Сохранить")
            shortcut: StandardKey.Save
            onTriggered: console.log("Файл->Сохранить")
        }
        Action {
            text: qsTr("Сохранить как...")
            shortcut: StandardKey.SaveAs
            onTriggered: console.log("Файл->Сохранить как...")
        }
        MenuSeparator { }
        Action {
            text: qsTr("Выход")
            shortcut: StandardKey.Quit
            onTriggered: Qt.quit()
        }
    }
    Menu {
        title: qsTr("Правка")
        Action {
            text: qsTr("Настройки")
            onTriggered: console.log("Правка->Настройки")
        }
    }
    Menu {
        title: qsTr("Справка")
        Action {
            text: qsTr("О программе")
            onTriggered: console.log("Справка->О программе")
        }
    }
}
