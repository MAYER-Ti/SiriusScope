# SiriusScope Layered Architecture

This document defines the architectural layers of SiriusScope, their responsibilities, allowed dependencies, and forbidden dependencies.

It is the main reference for preserving architectural consistency during long-term development.

## 1. Architectural principle

SiriusScope uses a modular layered architecture.

The main goals are:

- keep the UI responsive;
- isolate hardware protocols from UI and domain logic;
- make simulator and real hardware interchangeable;
- keep signal processing testable;
- support continuous storage without blocking rendering;
- make future protocol, antenna, storage, and UI changes possible without rewriting the whole application.

The architecture must prevent accidental coupling between:

- QML and hardware protocols;
- QML and storage formats;
- UI rendering and DSP;
- domain models and Qt Quick types;
- simulator logic and UI logic.

## 2. Layers

SiriusScope is divided into the following logical layers:

```text
UI Layer
Presentation / Application Layer
Core / Domain Layer
Processing Layer
Infrastructure Layer
Hardware Adapter Layer
```

The physical repository structure may evolve, but code must still respect these logical boundaries.

## 3. UI Layer

### 3.1 Responsibility

The UI Layer is responsible for:

* rendering QML components;
* displaying prepared data;
* receiving lightweight user interaction;
* binding to application-facing models;
* calling application-level commands.

Main UI components:

* `MainWindow`;
* `MenuBar` / `Toolbar`;
* `SpectrumView`;
* `BandItem`;
* `WaterfallView`;
* `AntennaIndicator`;
* `ResultTable`;
* `StatusBar`;
* configuration dialogs.

### 3.2 Allowed logic

QML may contain:

* layout rules;
* visual state;
* simple interaction handling;
* simple formatting for display;
* bindings to exposed properties;
* calls to invokable application commands.

### 3.3 Forbidden logic

QML must not contain:

* UDP/TCP parsing;
* direct socket access;
* direct hardware command formatting;
* DSP algorithms;
* bearing calculation algorithms;
* high-rate sample aggregation;
* archive writing or reading;
* file rotation logic;
* long-running loops over signal data;
* blocking operations.

### 3.4 Dependency rules

The UI Layer may depend on:

* QML components;
* Qt Quick / Qt Controls;
* presentation/application models exposed to QML.

The UI Layer must not depend directly on:

* hardware adapter classes;
* protocol parser classes;
* storage writer classes;
* processing services;
* raw sample queues.

## 4. Presentation / Application Layer

### 4.1 Responsibility

The Presentation / Application Layer connects UI actions with application use cases.

It is responsible for:

* QML-facing models;
* application controllers;
* command routing;
* validation of UI requests;
* coordination of scan workflows;
* coordination of device settings;
* exposing UI-safe state;
* adapting domain state for presentation.

Typical components:

* `ApplicationController`;
* `SessionController`;
* `ScanController`;
* `DeviceControlFacade`;
* `WaterfallController`;
* `ResultTableController`;
* `SettingsService`;
* `FrequencyViewportModel`;
* QML-facing list/table models.

### 4.2 Examples

When the user changes a `BandItem` setting:

```text
QML BandItem
    -> application controller
    -> validated BandConfig
    -> BCO control interface
    -> BCO command adapter
    -> BCO applies receiver configuration and controls RPU internally
```

When the user starts sector scanning:

```text
AntennaIndicator
    -> ScanController
    -> AntennaMotionPlanner
    -> antenna control interface
    -> BearingFrameBus / BearingInputFrame collection
    -> processing / bearing service
    -> ResultTable model
    -> storage writer
```

### 4.3 Allowed dependencies

The Application Layer may depend on:

* Core / Domain models;
* processing service interfaces;
* infrastructure service interfaces;
* hardware adapter interfaces;
* Qt object/model types when exposing data to QML.

### 4.4 Forbidden dependencies

The Application Layer must not:

* parse raw hardware packets directly if parser classes exist;
* implement low-level UDP/TCP details;
* perform heavy DSP in UI-facing models;
* perform blocking file operations on the GUI thread;
* expose mutable raw domain internals directly to QML.

## 5. Core / Domain Layer

### 5.1 Responsibility

The Core / Domain Layer contains the central concepts of SiriusScope.

It is responsible for:

* stable domain data structures;
* business rules independent of UI;
* validation rules;
* time model;
* frequency model;
* bearing result model;
* recording metadata model;
* application-independent constraints.

Typical domain models:

* `SignalSample`;
* `BeamSample`;
* `BandConfig`;
* `ScanSector`;
* `TimeBase`;
* `WaterfallCell`;
* `WaterfallRow`;
* `BearingResult`;
* `ResultTableRow`;
* `RecordingMetadata`.

### 5.2 Domain rules

Domain code should define and enforce rules such as:

* valid amplitude range is `1..127`;
* input amplitude `0` is invalid;
* frequency offset is relative to the configured band/base frequency;
* full product frequency range is `0.3..18 GHz`;
* current iteration uses 5 `BandItem` objects;
* current iteration uses 2 beams;
* future 8-beam support must remain possible;
* original BCO `sampleIndex` must be preserved;
* UI display time must be derived from the domain time model.

### 5.3 Allowed dependencies

The Core / Domain Layer may depend on:

* C++ standard library;
* small, UI-independent utility types;
* carefully isolated Qt Core types only if unavoidable.

Prefer standard C++ types in domain models.

### 5.4 Forbidden dependencies

The Core / Domain Layer must not depend on:

* QML;
* Qt Quick;
* UI components;
* socket implementation classes;
* file archive implementation classes;
* concrete simulator classes;
* concrete hardware classes.

## 6. Processing Layer

### 6.1 Responsibility

The Processing Layer transforms incoming samples into higher-level data.

It is responsible for:

* validation and filtering of input samples;
* grouping samples by frequency, time, band, and beam;
* aggregation for `WaterfallView`;
* preparation of amplitude values by beam;
* bearing-related calculations;
* scan result preparation;
* processing diagnostics.

Possible components:

* `SampleValidator`;
* `SampleAggregator`;
* `WaterfallRowBuilder`;
* `BearingService`;
* `ScanProcessingService`;
* `SignalDetector`.

### 6.2 Current iteration note

RTS type recognition is out of scope for the current iteration.

Do not implement an active `SignalClassifier` or RTS-recognition workflow unless explicitly requested. Classification may be mentioned only as a future extension.

### 6.3 Performance rules

Processing code must be suitable for near-real-time operation.

It should:

* avoid blocking the GUI thread;
* support worker-thread execution;
* avoid unnecessary allocations in high-rate paths;
* expose bounded queues or controlled buffering where appropriate;
* provide diagnostics for invalid data, queue pressure, and dropped data.

### 6.4 Allowed dependencies

The Processing Layer may depend on:

* Core / Domain models;
* processing configuration;
* standard C++ utilities;
* algorithm libraries if introduced deliberately.

### 6.5 Forbidden dependencies

The Processing Layer must not depend on:

* QML;
* QML item classes;
* concrete UI rendering classes;
* concrete socket classes;
* concrete archive writer implementation unless explicitly isolated through an interface.

## 7. Infrastructure Layer

### 7.1 Responsibility

The Infrastructure Layer provides technical services not specific to UI rendering or hardware protocols.

It is responsible for:

* binary archive storage;
* result table storage;
* settings storage;
* metadata storage;
* file rotation;
* cache management;
* technical logging;
* diagnostics persistence;
* replay file reading where applicable.

Typical components:

* `BinaryWaterfallStorage`;
* `BinaryResultTableStorage`;
* `MetadataStorage`;
* `SettingsStorage`;
* `DiagnosticLogStorage`;
* `ArchiveIndex`;
* `FileRotationService`;
* `ReplayReader`.

### 7.2 Storage rules

Infrastructure must support:

* continuous recording;
* loading historical Waterfall rows after restart;
* loading historical ResultTable rows after restart;
* asynchronous file I/O;
* recoverable cache;
* non-fatal handling of corrupted or missing settings;
* diagnostics for storage errors.

Large runtime data must not be stored in QSettings.

### 7.3 Allowed dependencies

The Infrastructure Layer may depend on:

* Core / Domain models;
* infrastructure interfaces;
* filesystem libraries;
* Qt Core classes if useful for settings or files;
* serialization libraries if introduced deliberately.

### 7.4 Forbidden dependencies

The Infrastructure Layer must not depend on:

* QML UI components;
* concrete QML item state;
* hardware socket implementation details;
* rendering internals.

## 8. Hardware Adapter Layer

### 8.1 Responsibility

The Hardware Adapter Layer hides real hardware and simulator communication details behind stable interfaces.

It is responsible for:

* UDP reception from BCO;
* TCP reception from antenna / rotating device;
* BCO command formatting and sending, including receiver configuration that affects the RPU through the BCO;
* antenna command formatting and sending;
* protocol parsing;
* protocol version handling;
* simulator adapters;
* hardware diagnostics.

Typical components:

* `UdpBcoReceiver`;
* `TcpAntennaClient`;
* `ProtocolParserV1`;
* `ProtocolParserV2`;
* `BcoCommandAdapter`;
* `AntennaCommandAdapter`;
* `SimulatorBcoAdapter`;
* `SimulatorAntennaAdapter`.

### 8.2 Interface rule

Real hardware and simulator must implement the same application-level interfaces.

The rest of the application must be able to work with either source without changing UI or business logic.

Conceptual interface groups:

* BCO sample source;
* antenna azimuth source;
* BCO control;
* antenna control;
* protocol version selection;
* diagnostics source.

There must be no separate SiriusScope-to-RPU control interface. The RPU is outside the direct software boundary and is controlled by the BCO.

### 8.3 Protocol rules

Protocol details must be isolated.

Rules:

* UI must not parse protocol packets.
* Core models must not know packet binary layout.
* A new protocol version must not require rewriting QML.
* Unsupported protocol versions must create diagnostics, not crashes.
* Packet validation must happen before data enters core processing.

### 8.4 Allowed dependencies

The Hardware Adapter Layer may depend on:

* Core / Domain structures used as parsed output;
* application-level interfaces;
* networking libraries;
* byte parsing utilities;
* simulator utilities.

### 8.5 Forbidden dependencies

The Hardware Adapter Layer must not depend on:

* QML UI components;
* visual rendering classes;
* direct ResultTable UI state;
* direct WaterfallView UI state.

## 9. Dependency direction

Preferred dependency direction:

```text
UI
  -> Presentation / Application
      -> Core / Domain
      -> Processing interfaces
      -> Infrastructure interfaces
      -> Hardware interfaces

Processing
  -> Core / Domain

Infrastructure
  -> Core / Domain

Hardware Adapter
  -> Core / Domain
  -> Application-level interfaces
```

The key rule:

```text
Outer technical details may adapt to inner domain models,
but inner domain models must not depend on outer technical details.
```

## 10. Forbidden dependency examples

The following dependencies are forbidden:

```text
QML -> UDP socket
QML -> TCP socket
QML -> protocol parser
QML -> archive writer
QML -> binary file format
QML -> DSP algorithm
QML -> bearing algorithm

Domain -> QML
Domain -> Qt Quick
Domain -> concrete UDP receiver
Domain -> concrete archive writer

Processing -> QML item
Processing -> visual component
Hardware Adapter -> QML component
Infrastructure -> QML component
```

## 11. Threading model

### 11.1 Required separation

SiriusScope must be designed as a multithreaded or asynchronous application.

At minimum, the design must support separation of:

* GUI thread;
* BCO UDP reception;
* antenna TCP interaction;
* sample processing and aggregation;
* Waterfall row preparation;
* archive writing;
* archive reading / history preload;
* optional GPU/OpenGL preparation.

### 11.2 GUI thread rule

The GUI thread must not be blocked by:

* receiving packets;
* parsing packets;
* processing high-rate samples;
* writing files;
* reading old archive data;
* rotating archive files;
* long calculations.

### 11.3 Cross-thread communication

Use controlled communication mechanisms:

* Qt signals and slots;
* thread-safe queues;
* bounded queues;
* worker objects;
* explicit dispatchers;
* lock-free structures only when justified.

Avoid uncontrolled shared mutable state.

## 12. Data ownership principles

### 12.1 Raw input data

Raw packets belong to the Hardware Adapter Layer.

Raw packets must be parsed and validated before entering processing/domain logic.

### 12.2 Domain data

Domain data belongs to Core / Domain and Processing layers.

It must be represented by explicit types.

### 12.3 UI-ready data

UI-ready data belongs to Presentation / Application models.

QML receives prepared models, not raw hardware data.

### 12.4 Stored data

Stored data belongs to Infrastructure.

Storage formats must be isolated from UI and hardware protocol details.

## 13. Error handling and diagnostics

Recoverable errors must not crash the application when safe continuation is possible.

Examples:

* lost TCP antenna connection;
* missing UDP BCO data;
* invalid amplitude;
* unsupported protocol version;
* queue overflow;
* storage write failure;
* corrupted settings file;
* missing cache.

Errors must be:

* reported to application-level diagnostics;
* visible through `StatusBar`;
* written to technical logs when appropriate.

## 14. Testing implications

The architecture must make the following testable without QML:

* domain validation;
* time conversion;
* frequency calculations;
* bearing-related calculations;
* protocol parsers;
* storage read/write;
* file rotation;
* simulator behavior;
* processing aggregation.

When adding nontrivial logic, prefer implementing it outside QML so it can be covered by unit or integration tests.

## 15. Architecture change policy

Do not change layer boundaries or dependency direction casually.

An architecture change must be explicit and documented when it affects:

* public interfaces between layers;
* data ownership;
* threading model;
* storage format;
* hardware/simulator abstraction;
* current-vs-future scope separation.

For significant changes, add or update an ADR in:

```text
docs/architecture/adr/
```

## 16. Current known simplifications

Some current code may still be prototype-level.

Prototype code is acceptable temporarily if it does not become the permanent architecture by accident.

When replacing prototype logic:

* move business logic out of QML;
* replace stubs with interfaces and implementations;
* preserve simulator compatibility;
* add tests for moved logic;
* update documentation when responsibilities change.
