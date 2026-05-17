# To do list

[ ] Сделать "Глубокое" нажатие на кнопки поворота антенны

/
├── AGENTS.md
├── README.md
├── CONTEXT.md              # временно оставить, чтобы не ломать CMake
├── ARCHITECTURE.md         # временно оставить, чтобы не ломать CMake
│
└── docs/
    ├── README.md
    │
    ├── spec/
    │   ├── scope.md        # из CONTEXT.md, но скорректировать
    │   └── glossary.md     # новый файл
    │
    ├── architecture/
    │   ├── layers.md       # из ARCHITECTURE.md, но скорректировать
    │   └── data-flow.md    # новый короткий файл
    │
    └── development/
        └── build-and-test.md
		
		
		Да, в целом понятно. Документация уже задает хорошую рамку: текущий scope, запреты для QML, слои, поток данных,
  хранение, симулятор, тесты и приоритеты источников описаны достаточно ясно, чтобы по ним работать.

  Нашел несколько мест, которые лучше поправить до активной разработки:

  1. Карта документации ссылается на файлы, которых нет.
     В /C:/Qt/no_vector_projects/SiriusScope/docs/README.md:28 и /C:/Qt/no_vector_projects/SiriusScope/AGENTS.md:32
     перечислены docs/domain/*, docs/ui/*, docs/hardware/*, docs/storage/*, но этих файлов сейчас нет. Для Codex это
     будет выглядеть как обязательные документы, которые невозможно прочитать. Лучше либо создать короткие stub-файлы с
     TBD, либо пометить в docs/README.md, что они запланированы, но еще не существуют.
  2. Есть небольшое архитектурное расхождение по слоям.
     В /C:/Qt/no_vector_projects/SiriusScope/AGENTS.md:62 перечислены 5 слоев без отдельного Processing Layer, а /C:/Qt/
     no_vector_projects/SiriusScope/docs/architecture/layers.md:30 вводит отдельный Processing Layer. Это разумный слой
     для агрегации/пеленгации, но его надо добавить и в AGENTS.md, иначе инструкции конфликтуют.
  3. ТЗ местами смешивает hardware/storage в Infrastructure.
     В /C:/Qt/no_vector_projects/SiriusScope/docs/spec/SiriusScope_TZ_v0.1.md:133 аппаратные адаптеры и storage
     перечислены внутри Infrastructure layer, а отдельные документы уже разделяют Infrastructure Layer и Hardware
     Adapter Layer. Я бы считал новую архитектурную документацию более точной, но в ТЗ стоит явно написать, что это
     раннее укрупненное разделение или обновить схему.
  4. Стек разработки не до конца согласован.
     /C:/Qt/no_vector_projects/SiriusScope/docs/spec/scope.md:79 и build guide говорят C++20 / Qt 6 / QML / CMake, а ТЗ
     добавляет обязательные Boost и Conan в /C:/Qt/no_vector_projects/SiriusScope/docs/spec/SiriusScope_TZ_v0.1.md:183.
     В текущем CMakeLists.txt Boost/Conan не используются. Нужно решить: они обязательны, допустимы при необходимости,
     или это устаревший пункт.
  5. Markdown-косметика: несколько блоков закрыты четырьмя backtick вместо трех.
     Например /C:/Qt/no_vector_projects/SiriusScope/docs/spec/scope.md:55, /C:/Qt/no_vector_projects/SiriusScope/docs/
     architecture/layers.md:39, /C:/Qt/no_vector_projects/SiriusScope/docs/development/build-and-test.md:24. Это обычно
     рендерится нормально, но выглядит как опечатка и лучше унифицировать.

  Главное: требования понятны. Самое важное перед дальнейшей работой — закрыть отсутствующие документы или явно
  обозначить их как planned, и синхронизировать Processing Layer / Boost / Conan между AGENTS.md, scope и ТЗ.
  
  Цвет середины WaterfallView сделать желтым.







Ниже — план исправлений по результатам ручного теста. Я опираюсь на актуальный `main`, последний найденный коммит в поиске GitHub — `6cc96ca...`.

# Краткий диагноз

В текущей реализации `ScanController` уже есть единая скорость `scanSpeedDegPerSec`, состояние сканирования, progress и команды ручного движения. Это видно в `scancontroller.h`: контроллер содержит `scanSpeedDegPerSec`, `scanProgress`, `driveLeft`, `driveRight`, `startScan`, `startSelectedSectorScan`. 

Но в UI скорость и progress добавлены как отдельная вставка в блок управления антенной: используется стандартный `SpinBox`, стандартный `ProgressBar`, англоязычный label `Speed`, и это визуально выбивается из общего интерфейса `Theme` / `ControlButton` / chip-стиля. 

Вторая проблема — ручное движение антенны не использует общую настройку скорости. В `AntennaIndicator.qml` кнопки ручного движения всё ещё передают фиксированные скорости `1` и `10`, хотя рядом уже есть `scanSpeed`.  В `Main.qml` эти значения напрямую передаются в `ScanController.driveLeft(speed)` и `driveRight(speed)`. 

Третья проблема — `AntennaMotionPlanner::planSectorScan()` всегда строит путь через `min(leftCoord, rightCoord)` → `max(leftCoord, rightCoord)`. Поэтому planned start всегда оказывается на одной стороне безопасной координаты, а `ScanController::startScan()` всегда сначала делает `moveToAzimuth(plannedPath.startAzimuthDeg)`.  

---

# План исправления

## 1. Привести интерфейс скорости и progress bar к общему стилю

### Проблема

Сейчас блок скорости выглядит как технически добавленный элемент, а не как часть интерфейса SiriusScope:

```qml
Label { text: qsTr("Speed") }
SpinBox { ... }
ProgressBar { ... }
Label { text: scanActive ? "50%" : scanStateText }
```

Это выбивается из общего стиля, где используются тёмные панели, `Theme.chipBackground`, `Theme.panelBorder`, моноширинный шрифт, компактные chips и кастомные `ControlButton`. Цветовая система уже централизована в `Theme.qml`.  Есть и готовый стиль статусных чипов в `StatusChip.qml`. 

### Что сделать

Создать два локальных или отдельных QML-компонента:

```text
src/ui/components/AntennaIndicator/ScanSpeedControl.qml
src/ui/components/AntennaIndicator/ScanProgressIndicator.qml
```

Либо, если не хочется плодить файлы, сделать `component ScanSpeedControl` и `component ScanProgressIndicator` внутри `AntennaIndicator.qml`.

### Как должен выглядеть блок скорости

Заменить стандартный `SpinBox` на визуально совместимый control:

```text
[Скорость]  [-]  10°/с  [+]
```

Требования:

* фон `Theme.chipBackground`;
* border `Theme.panelBorder`;
* radius `Theme.radiusInset`;
* шрифт `Theme.monoFontFamily`;
* русский label `Скорость`, не `Speed`;
* единицы измерения `°/с`;
* disabled-состояние во время сканирования должно быть визуально понятно, но не ломать компоновку.

### Как должен выглядеть progress

Заменить стандартный `ProgressBar` на кастомный прямоугольник:

```qml
Rectangle {
    color: Theme.chipBackground
    border.color: Theme.panelBorder
    radius: Theme.radiusInset

    Rectangle {
        width: parent.width * ScanController.scanProgress
        color: Theme.signalCyan
        radius: Theme.radiusInset
    }

    Text {
        text: ScanController.scanActive
              ? Math.round(ScanController.scanProgress * 100) + "%"
              : ScanController.scanStateText
    }
}
```

Но не обязательно именно так — главное, чтобы progress bar визуально соответствовал тёмной теме и панели управления.

### Где править

```text
src/ui/components/AntennaIndicator/AntennaIndicator.qml
src/ui/Theme.qml — только если нужны новые цвета/размеры
```

### Критерии готовности

* В интерфейсе нет стандартного неоформленного `SpinBox`.
* В интерфейсе нет стандартного неоформленного `ProgressBar`.
* Label `Speed` заменён на `Скорость`.
* Блок скорости и progress визуально похожи на остальные chips / кнопки.
* При активном сканировании progress отображается аккуратно и не меняет резко высоту блока.

---

## 2. Сделать одну общую скорость для ручного движения и секторного сканирования

### Проблема

Сейчас есть единая настройка `ScanController.scanSpeedDegPerSec`, но ручные кнопки её фактически обходят. В `AntennaIndicator.qml` модель кнопок задаёт фиксированные значения:

```qml
{ label: "←10", speed: 10, direction: -1 }
{ label: "←1", speed: 1, direction: -1 }
{ label: "1→", speed: 1, direction: 1 }
{ label: "10→", speed: 10, direction: 1 }
```



То есть пользователь меняет скорость сканирования, но ручное движение продолжает жить в своей логике `1/10`.

### Рекомендуемое решение

Переименовать смысл свойства с узкого `scanSpeedDegPerSec` на более общий:

```cpp
antennaSpeedDegPerSec
```

или оставить старое имя для минимального изменения, но в UI трактовать его как **общую скорость антенны**.

Лучше архитектурно:

```cpp
Q_PROPERTY(double antennaSpeedDegPerSec READ antennaSpeedDegPerSec WRITE setAntennaSpeedDegPerSec NOTIFY antennaSpeedChanged)
```

А старое `scanSpeedDegPerSec` можно временно оставить как alias, чтобы не ломать QML и тесты сразу.

### Как должно работать

Одна настройка скорости используется для:

```text
ручное движение влево
ручное движение вправо
секторное сканирование
кнопка сканирования из TopToolbar
```

То есть вызовы должны стать такими:

```qml
onDriveLeftRequested: ScanController.driveLeft(ScanController.antennaSpeedDegPerSec)
onDriveRightRequested: ScanController.driveRight(ScanController.antennaSpeedDegPerSec)
onScanRequested: ScanController.startScan(leftAngle, rightAngle, ScanController.antennaSpeedDegPerSec)
```

### Что сделать с кнопками `←1`, `←10`, `1→`, `10→`

Я рекомендую убрать фиксированные скорости из этих кнопок и оставить:

```text
[←] [■] [→]
```

А скорость выбирать отдельным контролом `Скорость`.

Если хочется сохранить быстрые пресеты, лучше оформить их не как команды движения, а как изменение общей скорости:

```text
[1°/с] [10°/с] [30°/с]
```

Тогда ручное движение всё равно использует выбранное значение.

### Где править

```text
src/app/scancontroller.h
src/app/scancontroller.cpp
src/ui/Main.qml
src/ui/components/AntennaIndicator/AntennaIndicator.qml
tests/app/tst_scan_controller.cpp
```

### Что поправить в тестах

Добавить проверки:

```text
setAntennaSpeedDegPerSec(15)
driveLeft() использует 15°/с

setAntennaSpeedDegPerSec(20)
driveRight() использует 20°/с

setAntennaSpeedDegPerSec(12)
startSelectedSectorScan() использует 12°/с
```

Сейчас тесты проверяют только, что `driveLeft(10.0)` передал 10, но не проверяют связь общей скорости с ручным движением. 

### Критерии готовности

* В UI есть одна настройка скорости.
* Ручное движение и сканирование используют одну и ту же скорость.
* Нет ситуации, когда SpinBox показывает `20°/с`, а кнопка `←10` двигает на `10°/с`.
* Название в UI должно быть не “скорость сканирования”, а “скорость антенны” или просто “скорость”.

---

## 3. Исправить выбор стартовой стороны сектора

### Проблема

Сейчас `AntennaMotionPlanner::planSectorScan(left, right, options)` не знает текущий азимут антенны. Он берёт две safe-coordinate точки и всегда делает:

```cpp
safeStartCoord = std::min(leftCoord, rightCoord);
safeEndCoord = std::max(leftCoord, rightCoord);
```



Из-за этого сканирование всегда идёт от меньшей безопасной координаты к большей. А `ScanController::startScan()` затем всегда отправляет антенну к `plannedPath.startAzimuthDeg`. 

Требуемое поведение другое:

```text
Если антенна ближе к левой стороне сектора — начать слева и идти вправо.
Если антенна ближе к правой стороне сектора — начать справа и идти влево.
```

### Что изменить в модели планирования

Нужно передавать текущий азимут в planner.

Добавить новый метод:

```cpp
static DomainResult<PlannedScanPath> planSectorScanFromCurrentAzimuth(
    double leftAngleDeg,
    double rightAngleDeg,
    double currentAzimuthDeg,
    const ScanMotionOptions& options = {});
```

Либо расширить существующий метод:

```cpp
static DomainResult<PlannedScanPath> planSectorScan(
    double leftAngleDeg,
    double rightAngleDeg,
    double currentAzimuthDeg,
    const ScanMotionOptions& options = {});
```

Я бы не ломал старый метод, а добавил новый, потому что старый метод уже используется в `selectSector()` для валидации выбора сектора без необходимости учитывать текущий азимут.

### Расширить `PlannedScanPath`

Сейчас в `PlannedScanPath` есть только:

```cpp
startAzimuthDeg
endAzimuthDeg
safeStartCoordDeg
safeEndCoordDeg
spanDeg
```



Нужно добавить направление:

```cpp
enum class ScanDirection
{
    IncreasingSafeCoord,
    DecreasingSafeCoord
};

ScanDirection direction = ScanDirection::IncreasingSafeCoord;
```

Или проще:

```cpp
bool reverse = false;
```

Но enum понятнее.

### Логика выбора стартовой стороны

Алгоритм:

```text
1. Преобразовать leftAngle и rightAngle в safeCoord.
2. Найти minCoord и maxCoord.
3. Преобразовать currentAzimuth в currentSafeCoord.
4. Посчитать расстояние до minCoord и maxCoord.
5. Если current ближе к minCoord:
      start = minCoord
      end = maxCoord
      direction = IncreasingSafeCoord
   Иначе:
      start = maxCoord
      end = minCoord
      direction = DecreasingSafeCoord
```

Псевдокод:

```cpp
const auto leftCoord = toSafeCoord(leftAngleDeg);
const auto rightCoord = toSafeCoord(rightAngleDeg);

const auto minCoord = std::min(leftCoord, rightCoord);
const auto maxCoord = std::max(leftCoord, rightCoord);

const auto currentCoord = toSafeCoord(currentAzimuthDeg);

const auto distanceToMin = std::abs(currentCoord - minCoord);
const auto distanceToMax = std::abs(currentCoord - maxCoord);

if (distanceToMin <= distanceToMax) {
    path.safeStartCoordDeg = minCoord;
    path.safeEndCoordDeg = maxCoord;
    path.direction = IncreasingSafeCoord;
} else {
    path.safeStartCoordDeg = maxCoord;
    path.safeEndCoordDeg = minCoord;
    path.direction = DecreasingSafeCoord;
}
```

### Важно

`spanDeg` должен оставаться положительным:

```cpp
path.spanDeg = std::abs(path.safeEndCoordDeg - path.safeStartCoordDeg);
```

Иначе progress сломается.

### Обновить progress

Сейчас progress считается так:

```cpp
progress = (safeCoord - path.safeStartCoordDeg) / path.spanDeg;
```



Это работает только при движении от меньшей safeCoord к большей.

Нужно заменить на direction-aware расчёт:

```cpp
double traveled = 0.0;

if (path.direction == ScanDirection::IncreasingSafeCoord) {
    traveled = safeCoord - path.safeStartCoordDeg;
} else {
    traveled = path.safeStartCoordDeg - safeCoord;
}

progress = clamp(traveled / path.spanDeg, 0.0, 1.0);
```

### Обновить simulator movement

Сейчас `SimulatorAntennaAzimuthSource` при активной scan-команде всегда делает:

```cpp
distance = targetCoord - currentCoord;
currentCoord + step
```

То есть фактически двигается только в сторону увеличения safeCoord. 

Нужно сделать:

```cpp
const auto directionSign =
    command->safeEndCoordDeg >= command->safeStartCoordDeg ? 1.0 : -1.0;

const auto distance = command->safeEndCoordDeg - currentCoord;

if (abs(distance) <= step) {
    set end azimuth
} else {
    currentCoord += directionSign * step;
}
```

Но лучше не вычислять sign по start/end каждый раз, а передать `ScanDirection` в `AntennaSectorScanCommand`.

Сейчас `AntennaSectorScanCommand` уже содержит `safeStartCoordDeg` и `safeEndCoordDeg`, но simulator дополнительно проверяет, что `safeStartCoordDeg < safeEndCoordDeg`.  Это тоже нужно изменить: для reverse scan `safeStartCoordDeg > safeEndCoordDeg` должно быть допустимо.

### Где править

```text
src/core/antenna_motion_planner.h
src/core/antenna_motion_planner.cpp
src/hardware/interfaces/antenna_control.h
src/hardware/simulator/simulator_antenna_control.cpp
src/hardware/simulator/simulator_antenna_azimuth_source.cpp
src/app/scancontroller.cpp
tests/core/tst_antenna_motion_planner.cpp
tests/app/tst_scan_controller.cpp
tests/app/tst_scan_controller_simulator.cpp
tests/hardware/tst_simulator_antenna.cpp
```

### Критерии готовности

* Если сектор `40°..100°`, а текущий азимут `35°`, старт сканирования — `40°`.
* Если сектор `40°..100°`, а текущий азимут `110°`, старт сканирования — `100°`.
* Progress растёт от `0` до `1` и при прямом, и при обратном направлении.
* Симулятор физически двигает антенну в нужную сторону.
* `scanCompleted` срабатывает при достижении ближайшего/дальнего края сектора в обоих направлениях.

---

# 4. Рекомендуемый порядок реализации

## Шаг 1. Исправить planner и direction-aware scan path

Сначала исправить core-логику, потому что от неё зависит всё остальное.

Сделать:

```text
PlannedScanPath.direction
planSectorScanFromCurrentAzimuth(...)
direction-aware span/progress
```

Обновить тесты `tst_antenna_motion_planner`.

Добавить тесты:

```text
current azimuth near left side chooses left start
current azimuth near right side chooses right start
reverse scan keeps positive span
reverse scan progress can be calculated
blind zone boundary sectors still valid
```

---

## Шаг 2. Исправить `ScanController`

В `startScan()` заменить:

```cpp
planSectorScan(leftAngleDeg, rightAngleDeg, scanOptions(speed))
```

на:

```cpp
planSectorScanFromCurrentAzimuth(
    leftAngleDeg,
    rightAngleDeg,
    m_currentAzimuthDeg,
    scanOptions(speed))
```

В `updateAzimuth()` заменить формулу progress на direction-aware.

Также важно: `selectSector()` может продолжать использовать старый `planSectorScan()` только для валидации и сохранения выбранного сектора. А фактический путь должен строиться именно в `startScan()`, потому что к моменту старта текущий азимут мог измениться.

---

## Шаг 3. Исправить simulator

В `SimulatorAntennaControl` убрать ограничение:

```cpp
command.safeStartCoordDeg >= command.safeEndCoordDeg → invalid
```

Потому что обратное сканирование должно быть валидным.

В `SimulatorAntennaAzimuthSource` сделать движение по safe-coordinate с учётом знака:

```cpp
const auto direction = command->safeEndCoordDeg >= currentCoord ? +1.0 : -1.0;
nextCoord = currentCoord + direction * step;
```

И завершать движение, когда до targetCoord осталось меньше шага.

---

## Шаг 4. Унифицировать скорость

Переименовать в UI:

```text
Скорость сканирования → Скорость антенны
```

В `AntennaIndicator.qml` заменить фиксированные кнопки `←10`, `←1`, `1→`, `10→` на:

```text
[←] [■] [→]
```

и передавать в `driveLeftRequested` / `driveRightRequested` текущую скорость:

```qml
antennaIndicator.driveLeftRequested(antennaIndicator.scanSpeed)
antennaIndicator.driveRightRequested(antennaIndicator.scanSpeed)
```

Или ещё лучше — сигнал без аргумента:

```qml
signal driveLeftRequested()
signal driveRightRequested()
```

А в `Main.qml`:

```qml
onDriveLeftRequested: ScanController.driveLeft(ScanController.scanSpeedDegPerSec)
onDriveRightRequested: ScanController.driveRight(ScanController.scanSpeedDegPerSec)
```

---

## Шаг 5. Привести UI к стилю

Заменить стандартный `SpinBox` и `ProgressBar` на кастомные компоненты в стиле SiriusScope.

Минимально:

```text
- убрать Label "Speed";
- заменить на "Скорость";
- оформить значение скорости как chip;
- оформить progress bar через Rectangle;
- использовать Theme.signalCyan / Theme.chipBackground / Theme.panelBorder;
- сохранить высоту панели без скачков.
```

---

## Шаг 6. Прогнать ручной тест

Проверить сценарии:

```text
1. Изменить скорость на 5°/с.
2. Нажать ручное движение влево — антенна двигается 5°/с.
3. Нажать ручное движение вправо — антенна двигается 5°/с.
4. Запустить сканирование — сектор проходится 5°/с.
5. Изменить скорость на 20°/с.
6. Повторить ручное движение и сканирование.
7. Выбрать сектор, находясь ближе к левой стороне — старт слева.
8. Выбрать тот же сектор, находясь ближе к правой стороне — старт справа.
9. Progress bar всегда идёт от 0% к 100%.
10. UI визуально не выбивается из общего стиля.
```

Главное исправление по логике — **не пытаться чинить это только в QML**. Старт с ближайшей стороны должен быть рассчитан в C++ planner / `ScanController`, а UI должен только отображать выбранный сектор и отправлять команду.

