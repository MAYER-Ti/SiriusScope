# SiriusScope Glossary

This document defines the main project terms and abbreviations used in SiriusScope documentation, code, issues, and Codex tasks.

Use these terms consistently.

## Product and project terms

### SiriusScope

The new cross-platform desktop software product for изделие «Сириус».

SiriusScope provides hardware control, signal reception, visualization, continuous storage, sector scanning, and bearing calculation.

### Изделие «Сириус»

The radio-technical reconnaissance system for which SiriusScope is developed.

At the system level, изделие «Сириус» is intended for detecting, receiving, analyzing, and bearing radio emission sources.

### Current iteration

The currently implemented scope of SiriusScope.

The current iteration focuses on:

- signal reception;
- SpectrumView;
- WaterfallView;
- AntennaIndicator;
- sector scanning;
- bearing calculation;
- final result table;
- continuous storage;
- simulator support.

### Future extension

A feature that may be implemented later but is not part of the current iteration unless explicitly requested.

Examples:

- RTS type recognition;
- map background;
- external system export;
- 8-beam antenna support;
- long-term target tracking;
- advanced result-table filtering and export.

## Radio domain terms

### РТР

Радиотехническая разведка.

A field concerned with detecting, receiving, analyzing, and locating radio-technical emissions.

### РЭР

Радиоэлектронная разведка.

A broader term related to reconnaissance of electromagnetic emissions and radio-electronic systems.

### РТС

Радиотехническое средство.

A radio-technical system or device that emits signals, for example radar or other radio-emitting equipment.

### ИРИ

Источник радиоизлучения.

A source of radio emission.

In SiriusScope, bearing calculation is performed to estimate the direction toward an ИРИ.

### Signal

A received radio-frequency event or emission represented in SiriusScope through samples, aggregated data, visual rows, and scan results.

The exact level of abstraction depends on context:

- raw or parsed BCO sample;
- grouped sample set;
- visual Waterfall cell;
- bearing-related result.

### Emission

A radio emission produced by an ИРИ.

In most SiriusScope documents, “signal” is used as the practical software-level term.

### Bearing

Direction to a radio emission source.

In Russian project materials: `пеленг`.

In the current iteration, bearing is calculated and displayed per `BandItem`.

### Azimuth

Current angular position of the antenna or bearing result angle.

Usually expressed in degrees.

### Sector

Angular range selected by the operator on `AntennaIndicator` for scanning.

During sector scanning, the antenna moves through the selected sector and SiriusScope collects data for bearing calculation.

### Blind zone

Angular range where the antenna center must not be positioned.

In the current assumptions, the antenna has a blind zone around 170–190 degrees. Antenna control logic must account for this.

## Hardware terms

### АС

Антенная система.

The antenna system used to receive radio emissions.

### ПУ

Поворотное устройство.

The rotating device that changes the antenna direction and provides current azimuth.

### СВБ

Project-specific name or abbreviation used for the antenna rotating mechanism / antenna position source.

When used in software documentation, treat it as related to the antenna / rotating device interface unless a more precise hardware definition is provided.

### РПУ

Радиоприемное устройство.

Radio receiving device.

Responsible for radio reception and primary analog/radio-frequency operations such as amplification and frequency conversion.

SiriusScope does not control the RPU directly. The BCO controls the RPU internally, and SiriusScope sends reception configuration only to the BCO.

### БЦО

Блок цифровой обработки.

Digital processing unit.

In the current SiriusScope model, the BCO accepts reception configuration from SiriusScope, controls the RPU internally, and sends discrete signal samples to the software over UDP.

### Управляющая конфигурация БЦО

A structured set of reception settings that SiriusScope sends to the BCO.

It conceptually includes frequency ranges, dwell time, filter parameters, polarization, input/output attenuators, and other settings needed by the BCO to control reception and the RPU internally.

The BCO protocol may support up to 8 configured ranges; the current SiriusScope iteration uses 5.

Exact fields and binary/network representation are `TBD` until the dedicated BCO control protocol document is written.

### Аппаратная часть

The real hardware components of изделие «Сириус», including antenna system, rotating device, RPU, BCO, and related control interfaces. SiriusScope directly controls the BCO and antenna/rotating device, while RPU control is behind the BCO boundary.

### Simulator

A software imitation of the hardware.

The simulator must use the same application-level interfaces as real hardware. SiriusScope UI and business logic must not depend on whether data comes from real hardware or simulator.

### Replay

A mode where previously recorded data is read from file and used as an input source.

Replay is useful for testing, debugging, and analysis.

## Data and protocol terms

### UDP

User Datagram Protocol.

Used by the BCO data stream in the current SiriusScope architecture.

### TCP

Transmission Control Protocol.

Used by the antenna / rotating device azimuth stream in the current SiriusScope architecture.

### Protocol parser

A component responsible for converting raw UDP/TCP packets or messages into typed software structures.

Protocol parsers must be isolated from QML and UI code.

### Protocol version

Explicit version of a hardware or simulator protocol.

SiriusScope must be able to add new protocol versions without rewriting UI or core business logic.

### Sample

A discrete received data element from the BCO stream.

In the current conceptual model, a sample contains:

- `sampleIndex`;
- `frequencyOffsetHz`;
- `amplitude`;
- `beamIndex`.

### `sampleIndex`

The original sample number from the BCO processing session.

SiriusScope must preserve it.

It is used by `TimeBase` to compute local time and global display time.

### `frequencyOffsetHz`

Frequency offset in hertz relative to the base frequency configured for the related BCO-controlled reception band.

The absolute display frequency is computed from base frequency plus offset.

### `amplitude`

Signal amplitude in conditional units.

Current valid input range:

```text
1..127
```

Invalid input values include:

* 0;
* negative values;
* values above 127.

Invalid values must not crash SiriusScope.

### `beamIndex`

Index of the antenna beam associated with a sample.

Current iteration supports:

```text
0
1
```

Future 8-beam support must remain architecturally possible, but is not implemented in the current iteration.

### Beam

A directional antenna lobe / reception channel.

The current system uses two beams.

### `A0`

Aggregated amplitude for beam 0 in a frequency-time cell.

### `A1`

Aggregated amplitude for beam 1 in a frequency-time cell.

### Direction difference

Normalized difference between two beam amplitudes.

Common conceptual form:

```text
D = (A0 - A1) / (A0 + A1)
```

The exact sign convention must be kept consistent across domain logic, UI colors, and documentation.

### TimeBase

Domain model that links BCO sample numbering to local and global time.

It preserves:

* recording start time;
* first sample index;
* sample period.

It is used to compute:

* local time from recording start;
* global/system time for display;
* stable historical reconstruction.

### Local time

Time offset from the beginning of a recording.

### Global time

System/UTC-based time used for display in WaterfallView and ResultTable.

### Recording

A continuous stored sequence of data.

SiriusScope is designed around continuous recording rather than legacy fixed time sessions.

### Archive

Stored historical data that can be loaded after application restart.

The archive includes high-volume binary data, metadata, settings snapshots, technical logs, and optional indexes/cache.

### Metadata

Descriptive information about a recording or archive file.

Examples:

* recording ID;
* start time;
* end time;
* format version;
* application version;
* TimeBase values;
* band configuration;
* packet loss diagnostics.

## UI terms

### MainWindow

The main SiriusScope application window.

Current layout:

* menu/toolbar at top;
* SpectrumView and WaterfallView on the left;
* AntennaIndicator and ResultTable on the right;
* StatusBar at the bottom.

### SpectrumView

The UI component that displays frequency-domain information and `BandItem` objects.

It controls the visible frequency range of `WaterfallView`.

### BandItem

A frequency-band UI/domain object corresponding to one BCO-controlled reception band.

Current iteration uses exactly 5 `BandItem` objects.

Each `BandItem`:

* represents up to 500 MHz;
* has a fixed color;
* is used for bearing result color mapping;
* must not directly send hardware commands from QML.

The BCO protocol may support up to 8 configured ranges, but the current SiriusScope iteration exposes exactly 5 `BandItem` objects.

### WaterfallView

The frequency-time visualization component.

It displays signal history over time.

Current behavior target:

* frequency on horizontal axis;
* time on vertical axis;
* movement from top to bottom;
* color based on amplitude and two-beam direction difference.

### AntennaIndicator

Circular indicator for antenna and bearing visualization.

It displays:

* current antenna azimuth;
* selected scan sector;
* scanning progress;
* bearing results.

### ResultTable

Final scan result table.

It is not a detailed pulse table in the current iteration.

It displays scan-level results and must load previous results from storage on startup.

### StatusBar

Bottom status and diagnostics area.

It displays:

* application status;
* hardware connection status;
* current azimuth;
* recording status;
* latest errors and diagnostic messages.

### MenuBar / Toolbar

Top-level application commands and mode controls.

Detailed behavior is defined in UI documents as the product evolves.

## Architecture terms

### UI Layer

Layer responsible for QML views, lightweight bindings, and user interaction.

The UI layer must not perform DSP, protocol parsing, hardware control, storage, or heavy processing.

### Presentation Model Layer

Layer responsible for preparing data for QML and exposing safe view models.

Also called UI model layer in some documents.

### Application Layer

Layer responsible for orchestration, commands, use cases, controllers, and interaction between UI-facing models and core services.

### Core / Domain Layer

Layer containing domain models, core rules, and processing-independent concepts.

It must not depend on QML.

### DSP / Processing Layer

Layer responsible for signal processing, grouping, aggregation, detection-related logic, and bearing-related services.

The current iteration must avoid treating RTS classification as an active implemented component.

### Infrastructure Layer

Layer responsible for storage, settings, logs, caches, archive formats, and technical services.

### Hardware Adapter Layer

Layer responsible for hardware-facing clients and adapters:

* UDP BCO receiver;
* TCP antenna client;
* BCO command adapter;
* antenna command adapter;
* simulator adapters;
* protocol parsers.

### Adapter

A component that hides details of an external system or protocol behind a stable interface.

### Controller

A component that coordinates an application-level operation, such as scanning, updating settings, or requesting data.

### Service

A component that implements reusable business, processing, storage, or infrastructure logic.

### Model

A data structure or observable object used to represent domain state or UI-ready state.

## Storage terms

### Binary storage

File storage format used for high-volume runtime data such as Waterfall rows or result rows.

### Settings storage

INI or JSON-based storage for application settings.

Large runtime data must not be stored in settings.

### Technical log

File-based diagnostic log used for development, debugging, and field diagnostics.

### Rotation

Policy for limiting stored archive files and removing or replacing old data.

Rotation must not corrupt the current recording.

### Cache

Derived data used to speed up loading or display.

Cache must be recoverable and must not be required for application startup.

## Development terms

### MVP

Minimum viable product for the current development stage.

In SiriusScope, MVP must still respect architectural boundaries and real-time responsiveness requirements.

### Current scope

The set of features that may be implemented without special approval.

Defined in `docs/spec/scope.md`.

### Out of scope

Features that must not be implemented unless explicitly requested.

### TBD

To be determined.

Use `TBD` for unknown protocol details, exact binary formats, or hardware-specific values that are not finalized yet.

### ADR

Architecture Decision Record.

A short document that explains an important architecture decision, its context, and consequences.

### Codex task

A task given to Codex or another AI coding assistant.

A good Codex task should specify:

* goal;
* files or docs to read;
* target layer;
* allowed changes;
* forbidden changes;
* expected tests.

