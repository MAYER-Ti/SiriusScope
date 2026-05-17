# SiriusScope Data Flow

This document describes the runtime data flow in SiriusScope.

It explains how signal samples, BCO reception configuration, antenna azimuth, bearing results, UI updates, storage operations, simulator data, and diagnostics move through the system.

## 1. Data flow principles

SiriusScope must follow these principles:

- raw hardware packets do not enter QML;
- QML receives only prepared UI models;
- protocol parsing is isolated in hardware adapter classes;
- processing and storage must not block the GUI thread;
- simulator and real hardware must use the same application-level interfaces;
- SiriusScope sends receiver settings to the BCO only, never directly to the RPU;
- domain data must preserve original hardware identifiers such as `sampleIndex`;
- diagnostics must be propagated to `StatusBar` and technical logs.

## 2. Main runtime flows

SiriusScope has seven main runtime flows:

1. BCO signal sample flow.
2. BCO reception configuration flow.
3. Antenna azimuth flow.
4. Sector scanning and bearing flow.
5. Waterfall display flow.
6. Storage and history loading flow.
7. Diagnostics flow.

These flows are connected but must remain separated through explicit interfaces.

## 3. BCO signal sample flow

### 3.1 Purpose

The BCO signal sample flow receives discrete signal samples from the BCO over UDP, validates them, converts them into domain structures, and sends them to processing and storage paths.

### 3.2 Flow

```text
BCO UDP stream
    -> UdpBcoReceiver
    -> ProtocolParserV*
    -> SampleValidator
    -> SignalSample / BeamSample
    -> Processing queue
    -> Processing services
    -> UI-ready models
    -> Storage writer
```

### 3.3 Responsibilities

`UdpBcoReceiver`:

* receives UDP packets;
* does not interpret UI behavior;
* forwards raw packets to the selected protocol parser;
* reports packet-level diagnostics.

`ProtocolParserV*`:

* parses protocol-specific packet layout;
* checks protocol version;
* converts packet data into typed sample structures;
* rejects or marks malformed packets;
* does not depend on QML.

`SampleValidator`:

* validates domain-level constraints;
* rejects or marks invalid amplitude values;
* checks beam index validity for the current configuration;
* checks frequency offset assumptions;
* produces diagnostics for invalid data.

`Processing queue`:

* decouples network reception from processing;
* must be bounded or otherwise controlled;
* must produce diagnostics under pressure or data loss.

`Processing services`:

* aggregate samples;
* group samples by time/frequency/band/beam;
* prepare data for Waterfall rows;
* prepare data for bearing-related calculations.

## 4. Antenna azimuth flow

### 4.1 Purpose

The antenna azimuth flow receives the current antenna angle and makes it available for display, scanning, and bearing calculation.

### 4.2 Flow

```text
Antenna / rotating device TCP messages
    -> TcpAntennaClient
    -> AntennaProtocolParserV*
    -> AzimuthProvider
    -> Application state
    -> AntennaIndicator model
    -> Processing / bearing services
    -> Storage metadata when needed
```

### 4.3 Responsibilities

`TcpAntennaClient`:

* manages TCP connection to the antenna / rotating device;
* receives azimuth messages;
* reports connection state;
* reports lost connection or malformed messages.

`AntennaProtocolParserV*`:

* parses TCP message format;
* validates azimuth values;
* handles unsupported protocol versions.

`AzimuthProvider`:

* exposes current azimuth as a stable application-level data source;
* hides whether azimuth comes from hardware or simulator;
* provides time-associated azimuth values where needed.

`AntennaIndicator model`:

* exposes current azimuth to QML;
* exposes selected scan sector and scan progress;
* exposes final bearing marks.

## 5. Sector scanning and bearing flow

### 5.1 Purpose

The sector scanning flow coordinates operator sector selection, antenna movement, signal collection, bearing calculation, result display, and result storage.

### 5.2 Flow

```text
Operator selects sector in AntennaIndicator
    -> ScanController
    -> AntennaMotionPlanner
    -> IAntennaControl
    -> AntennaCommandAdapter or SimulatorAntennaAdapter
    -> antenna moves through selected sector
    -> IAntennaAzimuthSource updates current angle
    -> BCO samples are collected during scan
    -> SampleProcessor prepares BearingInputFrame
    -> BearingFrameBus
    -> ScanAcquisitionSession stores BearingFrameObservation values with current antenna azimuth
    -> BearingService
    -> BearingResult per BandItem
    -> AntennaIndicator model
    -> ResultTable model
    -> ResultTable storage
    -> Diagnostics / StatusBar
```

### 5.3 Rules

* QML must not directly command the antenna hardware.
* QML must call an application-level scan command.
* Antenna blind zone handling belongs to antenna-control/application logic, not QML visuals.
* `ScanController` coordinates scan sessions and passes collected `BearingFrameObservation` data to `BearingService`; it must not implement the bearing formula itself.
* `ScanController` must open scan acquisition only after the antenna starts the sector pass; frames collected while moving to the sector start are not part of bearing calculation.
* Bearing calculation must be performed outside QML.
* Bearing results must be computed per `BandItem`.
* Bearing result color must match the related `BandItem` color.
* ResultTable rows must be generated from domain/application results, not manually assembled in QML.
* The current `BearingService` formula is an MVP two-beam estimate. It is isolated in the Processing Layer so it can be replaced after calibration without rewriting QML, `ScanController`, simulator, or storage integrations.

### 5.4 Bearing result output

A `BearingResult` should conceptually contain:

* result time;
* related `BandItem` identifier;
* bearing azimuth;
* frequency or frequency set;
* confidence/quality value if available;
* diagnostic flags if needed.

Exact fields may evolve, but the result must remain independent from QML item state.

### 5.5 Scan acquisition session

When sector scanning starts, SiriusScope opens a dedicated scan acquisition
session. During this session, the application collects BCO-derived data prepared
by `SampleProcessor` as `BearingInputFrame` values and binds them to the current
antenna azimuth.

After the antenna completes the selected sector, the processing path is flushed,
the scan acquisition session is closed, and the collected
`BearingFrameObservation` values are passed to `BearingService`. Waterfall
recording may be started and stopped along the same scan lifecycle, but bearing
calculation uses the scan acquisition session, not Waterfall pixels.

Bearing results are displayed on `AntennaIndicator` using the color of the
corresponding `BandItem` and are forwarded through application-level sinks for
the final result table and archive storage.

## 6. Waterfall display flow

### 6.1 Purpose

The Waterfall display flow prepares frequency-time visualization data and exposes it to the UI without giving QML raw high-rate samples.

### 6.2 Flow

```text
SignalSample / BeamSample stream
    -> Sample aggregation
    -> WaterfallCell values
    -> WaterfallRowBuilder
    -> Waterfall model / render buffer
    -> WaterfallView renderer
    -> UI display
```

### 6.3 Waterfall row content

A Waterfall row conceptually contains:

* row time;
* frequency range;
* cell values;
* amplitude information;
* beam-related aggregated values;
* enough metadata to resample display when frequency viewport changes.

### 6.4 Color model flow

```text
A0 / A1 values per cell
    -> amplitude normalization
    -> beam difference calculation
    -> color mapping
    -> render buffer
    -> WaterfallView
```

Where:

* `A0` is aggregated amplitude for beam 0;
* `A1` is aggregated amplitude for beam 1;
* amplitude affects brightness/intensity;
* beam difference affects directional color shift.

QML may render already prepared colors or use a lightweight renderer interface, but it must not own the high-rate aggregation or domain color calculations.

### 6.5 SpectrumView interaction

`SpectrumView` controls the visible frequency viewport.

Flow:

```text
Operator zooms/pans SpectrumView
    -> FrequencyViewportModel
    -> WaterfallController
    -> Waterfall display range update
    -> WaterfallView redraw / resampling
```

Rules:

* changing the viewport must not clear stored Waterfall history;
* old rows must remain available;
* old rows may be visually resampled into the new coordinate system;
* data outside the current visible range may move outside the visible area;
* heavy resampling or history loading must not block the GUI thread.

### 6.6 Waterfall sessions and timeline viewport

The current UI runtime uses an application-layer Waterfall session model before
persistent archive storage is introduced.

Rules:

* SiriusScope starts with Waterfall recording disabled;
* incoming live Waterfall rows are ignored while recording/session mode is off;
* `startRecording` creates a new in-memory session and enables live Waterfall flow;
* `stopRecording` closes the active in-memory session and freezes live Waterfall flow;
* while recording is off, the operator may browse already available sessions;
* render rows, horizontal grid lines, and time labels must be derived from the same
  timeline viewport and mapper;
* the Waterfall frequency band overlay remains frequency-only and does not scroll
  along the time axis.

### 6.7 Band configuration / BCO control flow

`BandItem` edits produce reception configuration for the BCO.

Flow:

```text
Operator edits BandItem settings
    -> Application controller
    -> validated BCO reception configuration
    -> BcoCommandAdapter
    -> BCO control protocol
    -> BCO applies receiver settings and controls RPU internally
    -> Diagnostics / StatusBar
```

Rules:

* SiriusScope must not send commands to the RPU directly;
* QML must not build BCO protocol payloads;
* the BCO protocol may support up to 8 configured ranges;
* the current SiriusScope workflow sends 5 ranges from the 5 `BandItem` objects;
* dwell time, filter parameters, polarization, and attenuators belong to the BCO reception configuration;
* exact BCO control protocol fields are `TBD` until the dedicated protocol document is written.

## 7. Storage flow

### 7.1 Purpose

The storage flow continuously records useful data and restores history after application restart.

### 7.2 Write flow

```text
Parsed and validated data
    -> Processing / domain results
    -> Storage queue
    -> BinaryWaterfallStorage
    -> BinaryResultTableStorage
    -> MetadataStorage
    -> DiagnosticLogStorage
    -> Archive indexes/cache
```

### 7.3 Read / history flow

```text
Application startup or user scrolls history
    -> Archive index lookup
    -> Async archive reader
    -> Historical Waterfall rows / ResultTable rows
    -> Presentation models
    -> WaterfallView / ResultTable
```

### 7.4 Rules

* continuous recording must be part of the design;
* high-volume data must be stored in binary files;
* settings and metadata may use INI/JSON/text formats;
* file I/O must not block the GUI thread;
* archive corruption must not prevent application startup when safe recovery is possible;
* cache must be recoverable and non-critical;
* rotation must not corrupt the current recording.

## 8. Settings flow

### 8.1 Purpose

Settings flow loads and saves application configuration.

### 8.2 Flow

```text
Application startup
    -> SettingsStorage
    -> AppSettings
    -> Application services / UI models

Operator changes settings
    -> Application controller
    -> validated settings
    -> SettingsStorage
```

### 8.3 Rules

* settings may be stored in INI or JSON;
* missing or corrupted settings must fall back to defaults;
* large runtime data must not be stored in settings;
* QSettings, if used, must be hidden behind an abstraction.

## 9. Simulator flow

### 9.1 Purpose

The simulator flow allows SiriusScope to be developed and tested without real hardware.

### 9.2 Flow

```text
Simulator source
    -> same BCO sample source interface
    -> same antenna azimuth source interface
    -> same BCO/antenna control interfaces
    -> Application / Processing / UI
```

### 9.3 Rules

* simulator must not require special UI paths;
* simulator must not bypass application/domain interfaces;
* simulator must support testing of SpectrumView, WaterfallView, AntennaIndicator, storage, and bearing flow;
* simulator data must be accepted by the same processing path as real data;
* BCO simulator behavior must include reception-configuration handling and internal RPU-control imitation behind the BCO interface.

## 10. Replay flow

### 10.1 Purpose

Replay uses previously recorded data as an input source for testing, debugging, or analysis.

### 10.2 Flow

```text
ReplayReader
    -> recorded samples / rows / events
    -> application input interfaces
    -> processing or presentation models
    -> UI
```

### 10.3 Rules

* replay must not be implemented as a QML-only feature;
* replay should use the same or compatible interfaces as live data;
* replay must preserve original sample/time metadata where available.

## 11. Diagnostics flow

### 11.1 Purpose

Diagnostics flow collects errors, warnings, and operational state and makes them visible to the operator and technical logs.

### 11.2 Flow

```text
Subsystem event
    -> Diagnostics service
    -> Application status model
    -> StatusBar
    -> DiagnosticLogStorage
```

### 11.3 Diagnostic sources

Diagnostics may come from:

* UDP reception;
* TCP connection;
* protocol parsing;
* invalid samples;
* unsupported protocol version;
* queue overflow;
* dropped packets;
* delayed processing;
* storage read/write errors;
* corrupted settings;
* simulator failures;
* rendering performance issues.

### 11.4 Rules

* recoverable errors must not crash the application when safe continuation is possible;
* diagnostics must be visible in `StatusBar`;
* technical details should be written to log files;
* diagnostics should include enough context for debugging;
* UI should not directly inspect low-level socket or file errors.

## 12. Startup data flow

### 12.1 Purpose

On startup, SiriusScope must initialize settings, storage, hardware/simulator mode, and UI models.

### 12.2 Flow

```text
Application start
    -> load settings
    -> initialize diagnostics
    -> initialize archive/index access
    -> restore ResultTable rows
    -> restore visible or recent Waterfall history
    -> select hardware/simulator/replay mode
    -> initialize application controllers
    -> initialize QML models
    -> show MainWindow
```

### 12.3 Rules

* missing settings must not prevent startup;
* missing cache must not prevent startup;
* corrupted old archive files must be isolated when possible;
* startup must not perform long blocking archive reads on the GUI thread.

## 13. Shutdown data flow

### 13.1 Purpose

On shutdown, SiriusScope must stop receiving data, flush important storage operations, and close resources safely.

### 13.2 Flow

```text
Application shutdown
    -> stop scan/session commands
    -> stop hardware/simulator receivers
    -> drain or safely discard processing queues
    -> flush storage metadata
    -> close log files
    -> release resources
```

### 13.3 Rules

* shutdown must not corrupt current archive metadata;
* long blocking shutdown operations should be bounded;
* partial recordings should remain diagnosable through metadata/logs.

## 14. Threading expectations

Data flow should be implemented with explicit threading or asynchronous boundaries.

Expected separation:

```text
GUI thread
    QML rendering and lightweight model binding

BCO receiver thread
    UDP receive and packet handoff

Antenna thread
    TCP receive and antenna state handoff

Processing worker
    validation, aggregation, Waterfall row building, bearing preparation

Storage worker
    archive writes, metadata writes, log writes, rotation

History loading worker
    archive reads and preload for scrollback
```

The exact implementation may vary, but the GUI thread must stay responsive.

## 15. Data flow anti-patterns

Avoid these patterns:

```text
QML reads UDP packets directly.
QML writes binary archive files directly.
QML calculates bearing from raw samples.
QML loops over high-rate sample arrays.
Protocol parser updates visual QML items directly.
Storage writer calls QML methods directly.
Domain model imports QtQuick types.
Simulator uses a separate UI-only code path.
Viewport change clears Waterfall history.
Archive read blocks UI during scrollback.
```

## 16. Testing implications

Data flow should be testable at the boundaries.

Recommended tests:

* parser converts raw packets to samples;
* invalid samples are rejected or flagged;
* sample queues handle pressure predictably;
* TimeBase converts sample indices correctly;
* WaterfallRowBuilder aggregates cells correctly;
* BearingService produces results per `BandItem`;
* storage writes and reads historical rows;
* simulator uses the same interfaces as hardware;
* diagnostics are produced for recoverable failures.

## 17. Open points

The following details are intentionally not finalized here:

* exact BCO UDP packet layout;
* exact antenna TCP message layout;
* exact BCO control protocol, including reception ranges, dwell time, filters, polarization, attenuators, and diagnostics;
* exact antenna command formats;
* exact bearing algorithm;
* exact archive binary layout;
* final threading primitives.

These details must be documented in their own files when finalized.

