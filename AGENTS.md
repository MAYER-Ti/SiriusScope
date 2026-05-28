# Repository Guidance for Codex

Любое описание commit пиши на русском языке.

This file is the short operational contract for AI-assisted work in SiriusScope.
Detailed requirements live in `docs/`.

## Project Summary

SiriusScope is a Qt/C++ desktop system for изделие «Сириус», a radio-technical
reconnaissance system.

The product direction is high-load BCO stream processing with explicit data plane /
control plane separation. SiriusScope must receive a high-speed BCO stream, process it
with predictable latency, calculate bearing, build waterfall/spectrum summaries, estimate
signal parameters, write continuous data, and display aggregated operator-facing results.

SiriusScope is not a target MVP/demo GUI architecture. Any current demo-rate path must be
treated as legacy/current implementation unless it is explicitly documented as target
architecture.

Future extensions unless explicitly requested:

- RTS type recognition;
- map background;
- external system export;
- advanced analytics;
- long-term target tracking;
- implemented 8-beam antenna support.

## Source Of Truth

Before changing code, read this file first.

Then read only the documents relevant to the task:

- `docs/README.md` - documentation map;
- `docs/spec/scope.md` - current scope;
- `docs/spec/glossary.md` - domain terms;
- `docs/architecture/layers.md` - layer boundaries;
- `docs/architecture/data-flow.md` - runtime data flow;
- `docs/architecture/high-load-data-plane.md` - high-load data plane target;
- `docs/architecture/baseline.md` - current/legacy implementation notes;
- `docs/storage/archive-format.md` - storage rules;
- `docs/development/build-and-test.md` - build, test, and performance checks;
- `docs/spec/SiriusScope_TZ_v0.1.md` - full technical assignment.

If documents conflict, use this priority:

1. Current user task.
2. Technical assignment and current scope documents in `docs/spec/`.
3. Architecture and domain documents.
4. Existing code.
5. README.

## Non-Negotiable Rules

- Do not put DSP, protocol parsing, hardware control, storage, or heavy processing in
  QML.
- Do not access hardware protocols directly from UI.
- Do not pass high-load raw samples through QML, `QObject`, `QAbstractListModel`,
  `QMetaObject::invokeMethod`, or Qt queued signals.
- Do not use `WaterfallController`, `SignalSampleBus`, or `BearingFrameBus` as
  production high-load raw data transports.
- Keep UI, application/control, core/domain, hardware/ingest, pipeline/data plane,
  DSP/processing, storage, and infrastructure concerns separated.
- Do not block the GUI thread with receiving, processing, rendering preparation, file
  I/O, history loading, or long calculations.
- Preserve compatibility with real hardware and simulator through the same interfaces.
- Add or update tests when changing nontrivial domain, parsing, buffering, storage,
  processing, aggregation, or bearing logic.

## Architecture Rules

Target runtime:

```text
BCO UDP / HighLoadSimulator
    -> RX / ingest thread
    -> preallocated block pool / memory pool
    -> bounded queues
    -> DSP / processing thread pool
    -> aggregators
    -> storage writer
    -> GUI snapshot publisher
    -> Qt/QML GUI
```

Logical layers:

```text
Qt adapter / presentation layer
Application / control layer
Core / domain layer
Hardware / ingest layer
Pipeline / data plane layer
DSP / processing layer
Storage layer
Infrastructure support layer
```

Forbidden dependencies:

```text
QML -> UDP/TCP protocol parser
QML -> raw stream queue
QML -> file archive writer
QML -> DSP algorithm
QML -> bearing algorithm
Application controller -> raw high-load sample vector
Processing/data plane -> QML item or QAbstractListModel
Domain model -> Qt UI type
```

## QML Rules

QML may:

- render UI;
- bind to prepared models;
- render immutable/downsampled snapshots;
- handle simple user interaction;
- call application-level commands.

QML must not:

- parse packets;
- write archives;
- calculate bearing;
- aggregate high-rate signal streams;
- perform heavy loops over incoming data;
- own long-running timers for data plane processing;
- receive raw high-load sample vectors.

## Hardware And Simulator Rules

Real hardware and simulator must use the same application-level and data plane
interfaces. The UI must not know whether data comes from:

- real BCO over UDP;
- real antenna controller over TCP;
- high-load simulator;
- UI demo simulator;
- replay file.

Protocol versions are isolated in parser/adapter classes. Unsupported versions produce
aggregated diagnostics, not crashes.

Simulator profiles:

- `UiDemo` - safe default for UI development.
- `MediumLoad` - intermediate load.
- `RealBcoEquivalent` - about 1,000,000 sample slots/s and 10 ms batches.
- `Stress150Percent` - overload/stress profile.

`RealBcoEquivalent` is not a safe ordinary default until the high-load data plane is
bounded, instrumented, and performance-tested.

## Current Domain Constraints

- SiriusScope works with 5 `BandItem` objects in the current workflow.
- Each `BandItem` represents one BCO band up to 500 MHz.
- 5 bands provide up to 2500 MHz of simultaneous observation.
- BCO control may support up to 8 configured frequency ranges; SiriusScope uses 5.
- SiriusScope does not interact with the RPU directly.
- Receiver settings are sent to the BCO; the BCO controls the RPU internally.
- Full product frequency range is `0.3..18 GHz`.
- Current antenna model uses `beamIndex = 0` and `beamIndex = 1`.
- Future 8-beam support must not be made impossible.
- Input amplitude range is `1..127`.
- Amplitude value `0` is invalid for input samples.
- Invalid input data is rejected or marked diagnostically without crashing.
- Bearing calculation is the main task of the current product iteration.

## Time Model Rules

Preserve original BCO `sampleIndex`.

Use a dedicated time model to derive:

- local time from recording start;
- global UTC/system time;
- display time for WaterfallView and result table.

Do not replace `sampleIndex` with UI time. Do not use exact `sampleIndex` as the only
high-load bearing pairing key; target pairing uses time/frequency/band windows.

## Storage Rules

SiriusScope must preserve data between launches.

High-volume data belongs to an asynchronous storage pipeline:

- binary;
- chunked;
- append-only;
- indexed;
- bounded by backpressure and metrics.

Settings and metadata may use INI, JSON, QSettings, or SQLite where appropriate, but
these formats are not raw high-rate stream stores.

File I/O must not block the GUI thread.

## Diagnostics And Metrics Rules

Operator diagnostics are aggregated warnings and state changes. Per-sample or
per-candidate diagnostics are forbidden on the high-load path.

Required pipeline metrics include:

- input MB/s;
- processed MB/s;
- dropped blocks;
- queue depth;
- RX latency;
- DSP latency;
- storage latency;
- GUI snapshot FPS;
- max block age;
- block pool usage.

Diagnostics publication must be rate-limited before logs or UI.

## Testing Rules

Add tests for:

- domain models;
- time conversion;
- protocol parsers;
- invalid input handling;
- block pool and bounded queue behavior;
- storage read/write and recovery;
- file rotation;
- bearing-related calculations;
- waterfall/spectrum/bearing aggregation;
- diagnostics rate limiting;
- simulator profiles and performance behavior.

Target coverage for the project is at least 50%.

For UI-only changes, run the application and manually verify the affected view.

## Build Commands

Configure debug build:

```bash
cmake --preset qt-win-mingw-debug
```

Build:

```bash
cmake --build build/win-mingw-debug
```

Run tests:

```bash
ctest --test-dir build/win-mingw-debug --output-on-failure
```

Keep concrete CMake build trees under `build/`; do not use the repository-root `build/`
directory itself as the build tree.

## Change Discipline

Prefer small, scoped changes.

When implementing a task:

1. Read the relevant docs.
2. Identify the target layer and whether the work belongs to data plane or control plane.
3. Avoid unrelated refactoring.
4. Keep public interfaces stable unless the task requires changing them.
5. Add or update tests for business logic and high-load behavior.
6. Update docs when behavior, architecture, or constraints change.

Do not silently introduce new product features.
Do not remove simulator compatibility.
Do not weaken real-time responsiveness requirements.
Do not move temporary prototype logic into permanent architecture without documenting it.
