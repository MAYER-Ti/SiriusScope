# SiriusScope Scope

This document defines the current product scope of SiriusScope.

It separates:

- the general capabilities of изделие «Сириус»;
- the current SiriusScope software iteration;
- future extensions that must not be implemented unless explicitly requested.

## 1. Product context

Изделие «Сириус» is a radio-technical reconnaissance system.

At the product/system level, изделие «Сириус» is intended for:

- detection and reception of radio-technical signal emissions;
- analysis of received signals;
- measurement of frequency, time, and amplitude parameters;
- bearing calculation for radio emission sources;
- display of received information;
- integration with related systems.

SiriusScope is the new desktop software component for изделие «Сириус».

It is developed to replace the fragmented workflow of two separate legacy applications:

- software for hardware control;
- software for signal analysis.

The goal is to provide a unified, maintainable, and extensible software system for control, visualization, storage, and bearing-related analysis.

## 2. Current SiriusScope iteration

The current iteration of SiriusScope is focused on building a new software product from scratch.

It must provide:

- unified operator workspace;
- control of аппаратная часть through isolated adapters;
- reception of BCO signal data;
- reception of antenna azimuth data;
- frequency-domain visualization;
- frequency-time visualization;
- sector scanning;
- bearing calculation;
- final scan result display;
- continuous data storage;
- simulator support for development and testing.

The main functional goal of the current iteration is:

```text
Bearing calculation and visualization for received signals.
```

RTS type recognition is not part of the current iteration.

## 3. In-scope features

### 3.1 Application architecture

SiriusScope must be designed as a modular cross-platform desktop application.

The architecture must separate:

* UI;
* presentation/application models and controllers;
* core/domain logic;
* signal processing services;
* infrastructure services;
* hardware adapters;
* simulator adapters.

UI, processing, hardware communication, and storage must not be tightly coupled.

### 3.2 Technology stack

The target stack is:

* C++;
* C++20;
* Boost;
* Qt 6;
* Qt Quick / QML;
* CMake;
* Conan.

Boost and Conan are part of the target development stack. Boost may be used where it provides clear value over standard C++ or Qt facilities. Conan must be used for external dependency management when third-party dependencies are introduced or formalized.

The target Qt version is Qt 6.9.3.

Qt 6.8+ may be acceptable for development if the feature set remains compatible with the target version.

### 3.3 Hardware and simulator integration

SiriusScope must work with:

* real аппаратная часть;
* software simulator;
* replay/file-based sources where applicable.

Real hardware and simulator paths must use the same application-level interfaces.

The UI must not know whether data comes from real hardware, simulator, or replay.

### 3.4 Input data

The current system expects two primary input streams:

1. BCO signal samples over UDP.
2. Antenna / rotating device azimuth over TCP.

SiriusScope also sends reception configuration to the BCO. The BCO controls the RPU internally; SiriusScope must not connect to or command the RPU directly.

BCO samples conceptually contain:

* `sampleIndex`;
* `frequencyOffsetHz`;
* `amplitude`;
* `beamIndex`.

Antenna data conceptually contains:

* current azimuth angle;
* connection/diagnostic state.

Exact packet formats may remain `TBD` until hardware protocols are finalized.

### 3.5 Frequency model

The full product frequency range is:

```text
0.3 GHz to 18 GHz
```

The current SiriusScope iteration uses:

* 5 `BandItem` objects;
* each `BandItem` represents one BCO band;
* each BCO band is up to 500 MHz wide;
* total simultaneous observation width is up to 2500 MHz.

The BCO control protocol is expected to support configuration of up to 8 frequency ranges, but the current SiriusScope UI and domain workflow use only 5 ranges.

The current UI must not allow the operator to add or remove `BandItem` objects unless a future task explicitly changes the scope.

The architecture must not make future support for another number of bands impossible.

### 3.6 Antenna and beams

The current iteration assumes a two-beam antenna model:

```text
beamIndex = 0
beamIndex = 1
```

Future support for an 8-beam antenna must remain architecturally possible, but is not part of the current iteration.

The antenna has a blind zone that must be considered by antenna-control logic when sector scanning is implemented.

### 3.7 Amplitude constraints

Input amplitude values are expected in the range:

```text
1..127
```

Rules:

* amplitude `0` is invalid for an input sample;
* negative amplitudes are invalid;
* amplitudes above `127` are invalid;
* invalid values must not crash the application;
* invalid values must be rejected, ignored, or marked diagnostically.

### 3.8 Time model

SiriusScope must preserve the original BCO `sampleIndex`.

The application must provide a time model that supports:

* local time from the start of recording;
* global/system time for display;
* stable reconstruction of historical data after restart;
* synchronization between SpectrumView, WaterfallView, AntennaIndicator, and ResultTable.

The UI must not replace the domain time model with ad hoc visual timestamps.

### 3.9 SpectrumView and BandItem

`SpectrumView` is responsible for:

* displaying the frequency scale;
* displaying 5 `BandItem` objects;
* changing the visible frequency viewport;
* driving the visible range of `WaterfallView`.

`BandItem` is responsible for representing and editing settings for one frequency band.

Band settings must be passed to application/controller logic first and then to the BCO control interface. QML must not directly create or send hardware commands. There is no SiriusScope-to-RPU control path.

### 3.10 WaterfallView

`WaterfallView` is responsible for frequency-time visualization.

It must support:

* display of signal history;
* movement from top to bottom;
* visible time scale;
* response to SpectrumView viewport changes;
* preservation of old rows when viewport changes;
* future asynchronous loading of old data from files;
* color model based on amplitude and two-beam difference.

The current implementation may start as an MVP, but it must not introduce architecture that prevents GPU/OpenGL or other accelerated rendering later.

### 3.11 AntennaIndicator

`AntennaIndicator` is responsible for:

* displaying current antenna azimuth;
* displaying selected scan sector;
* showing sector scanning progress;
* showing bearing results.

Bearing results must be displayed separately per `BandItem`.

Bearing result color must match the related `BandItem` color.

Current implementation note: bearing calculation is performed by `BearingService`
in the Processing Layer. It uses an MVP two-beam estimate for `beamIndex = 0`
and `beamIndex = 1`; the formula is not final and must remain replaceable
without changing QML, scan orchestration, simulator paths, or future storage.

### 3.12 ResultTable

The current iteration uses a final scan result table, not a detailed pulse table.

The result table must display scan results such as:

* global result time;
* antenna azimuth;
* frequency set / band-related frequencies.

The table is read-only for the operator in the current iteration.

It must support loading previously stored results after application startup.

### 3.13 StatusBar

`StatusBar` must display current system status and diagnostics.

It should include:

* program status;
* BCO connection status;
* antenna connection status;
* BCO/control status;
* reception-configuration status when reported by the BCO;
* current azimuth;
* recording status;
* latest errors and diagnostic messages.

A separate visible event log is not required in the current iteration.

Technical logs must still be written to files.

### 3.14 Storage

SiriusScope must preserve useful data between launches.

The current iteration must support storage for:

* Waterfall rows;
* final result table rows;
* metadata;
* settings;
* technical logs;
* indexes/cache for fast history access where needed.

Large runtime data must not be stored in QSettings.

Expected storage approach:

* binary files for high-volume data;
* INI or JSON for settings;
* JSON or similar text format for metadata;
* technical logs as files.

Continuous recording must be part of the product design.

File I/O must not block the GUI thread.

### 3.15 Testing

The project must be designed for testability.

Testable areas include:

* domain models;
* time conversion;
* protocol parsers;
* invalid input handling;
* storage read/write;
* file rotation;
* bearing-related calculations;
* processing and aggregation logic.

Target test coverage for the project is at least:

```text
50%
```

## 4. Out-of-scope features for the current iteration

The following features are not part of the current SiriusScope iteration unless a task explicitly requests them.

### 4.1 RTS type recognition

Recognition or classification of the radio-technical system type is out of scope.

Do not add `SignalClassifier` or RTS-recognition workflows as active current-version components.

It may be mentioned as a future extension.

### 4.2 Map background

Map display, geographic background, and geospatial target plotting are out of scope.

### 4.3 External system export

Transmission of results to external or related systems is out of scope.

### 4.4 Advanced result-table tools

The following table features are out of scope:

* export;
* sorting;
* filtering;
* advanced analytics;
* user editing of results.

### 4.5 Custom layout editing

User-controlled arbitrary resizing or full customization of all widgets is out of scope.

The application is designed for a fixed main layout in the current iteration.

### 4.6 8-beam antenna implementation

Future support for 8 beams must remain possible, but implementation is out of scope.

### 4.7 Long-term target tracking

Persistent history of targets on the antenna indicator and long-term target tracking are out of scope.

### 4.8 Direct Waterfall-to-table linking

Click-based synchronization between bearing marks, Waterfall rows, and ResultTable rows is out of scope for the current iteration.

### 4.9 Full protocol finalization

Exact hardware packet and command formats may remain `TBD`.

Architecture must still isolate protocol parsing so final formats can be added later.

## 5. Non-functional constraints

### 5.1 Responsiveness

The GUI must remain responsive during:

* data reception;
* data processing;
* rendering preparation;
* storage writes;
* archive reads;
* error handling;
* simulator operation.

The GUI thread must not perform long-running work.

### 5.2 Real-time behavior

SiriusScope must be designed for near-real-time operation.

The design must account for:

* continuous incoming data;
* bounded queues or controlled buffering;
* diagnostics for dropped or delayed data;
* asynchronous processing;
* stable rendering cadence.

### 5.3 Reliability

The application must handle recoverable failures without crashing when safe continuation is possible.

Recoverable failures include:

* lost antenna TCP connection;
* missing BCO UDP data;
* invalid sample values;
* unsupported protocol version;
* storage write error;
* corrupted settings file;
* missing cache.

Errors must be visible in StatusBar and written to technical logs.

### 5.4 Maintainability

The codebase must remain suitable for long-term development.

Required practices:

* small, cohesive modules;
* explicit interfaces between layers;
* minimal global state;
* no hidden cross-layer coupling;
* documentation updates when behavior changes;
* tests for nontrivial logic.

## 6. Current assumptions

The following assumptions are valid until replaced by more precise documents or implementation tasks:

* the application has one main window;
* the interface is primarily dark-themed;
* target operation is one monitor;
* 5 `BandItem` objects are visible in the current version;
* Waterfall history moves from top to bottom;
* bearing calculation is performed per `BandItem`;
* simulator is required for development and testing;
* real hardware and simulator must share the same interfaces;
* RPU is controlled by the BCO, not by SiriusScope;
* QML is presentation only and must not contain domain-heavy logic.

## 7. Open questions

The following items require future clarification:

1. Exact BCO UDP packet format.
2. Exact BCO control protocol for reception configuration, including up to 8 ranges, dwell time, filters, polarization, attenuators, and diagnostics.
3. Exact antenna TCP message format.
4. Exact antenna command format.
5. Exact bearing calculation model.
6. Exact grouping model for samples, pulses, and signal batches.
7. Target operating systems.
8. Minimum hardware requirements.
9. Final RGB values for WaterfallView and BandItem colors.
10. Final archive binary formats.
11. Final simulator protocol behavior.

Until these questions are resolved, implementations must use isolated interfaces and avoid hardcoding assumptions into UI code.
