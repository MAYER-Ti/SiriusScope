# Repository Guidance for Codex

This file is the short operational contract for AI-assisted work in SiriusScope.
Keep this file concise. Detailed requirements must live in `docs/`.

## Project summary

SiriusScope is a Qt/C++ desktop application for изделие «Сириус», a radio-technical reconnaissance system.

The current iteration focuses on:

- signal reception;
- real-time visualization;
- continuous data storage;
- sector scanning;
- bearing calculation;
- work with both real hardware and simulator through the same interfaces.

RTS type recognition, map background, export to external systems, advanced analytics, and long-term target tracking are future extensions unless explicitly requested.

## Source of truth

Before changing code, read this file first.

Then read only the documents relevant to the current task:

- `docs/README.md` — documentation map;
- `docs/spec/scope.md` — current scope and out-of-scope features;
- `docs/spec/glossary.md` — domain terms;
- `docs/architecture/layers.md` — layer boundaries;
- `docs/architecture/data-flow.md` — runtime data flow;
- `docs/domain/models.md` — domain models and formulas;
- `docs/ui/components.md` — UI component contracts;
- `docs/hardware/interfaces-and-protocols.md` — hardware and simulator boundaries;
- `docs/storage/archive-format.md` — storage and persistence rules;
- `docs/development/build-and-test.md` — build and test commands.
- `docs/spec/SiriusScope_TZ_v0.1.md` — full technical assignment. Read it when implementing new features, changing behavior, resolving requirement conflicts, or working on architecture-sensitive tasks.

If documents conflict, use this priority:

1. Current user task.
2. Technical assignment and current scope documents in `docs/spec/`.
3. Architecture and domain documents.
4. Existing code.
5. README.

## Non-negotiable rules

- Do not put DSP, protocol parsing, hardware control, storage, or heavy processing in QML.
- Do not access hardware protocols directly from UI.
- Keep UI, presentation/application, core/domain, infrastructure, and hardware-adapter concerns separated.
- Do not block the GUI thread with receiving, processing, rendering preparation, file I/O, or long calculations.
- Do not change architecture or public module boundaries unless the task explicitly asks for it.
- Do not implement out-of-scope features without a direct task.
- Preserve compatibility with both real hardware and simulator paths through the same interfaces.
- Add or update tests when changing nontrivial domain, parsing, buffering, storage, or processing logic.

## Architecture rules

Use layered architecture:

```text
UI layer
    QML views and lightweight bindings only.

Presentation / Application layer
    QML-facing models, controllers, commands, orchestration.

Core / Domain layer
    Signal models, time model, bearing results, processing-independent business rules.

Processing layer
    Sample validation, aggregation, Waterfall row preparation, bearing-related calculations.

Infrastructure layer
    Storage, settings, logging, binary formats, file rotation.

Hardware adapter layer
    UDP/TCP clients, protocol parsers, RPU/BCO/antenna command adapters, simulator adapters.
```

Dependency direction must remain stable:

```text
UI -> Application -> Core
Processing -> Core
Infrastructure -> Core
Hardware adapters -> Core/Application interfaces
```

Forbidden dependencies:

```text
QML -> UDP/TCP protocol parser
QML -> file archive writer
QML -> DSP algorithm
DSP/Core -> QML type
Domain model -> Qt UI type
```

## QML rules

QML may:

* render UI;
* bind to prepared models;
* handle simple user interaction;
* call application-level commands.

QML must not:

* parse packets;
* write archives;
* calculate bearing;
* aggregate high-rate signal streams;
* perform heavy loops over incoming data;
* own long-running timers for core processing.

## Hardware and simulator rules

Real hardware and simulator must use the same application-level interfaces.

The UI must not know whether data comes from:

* real BCO over UDP;
* real antenna controller over TCP;
* test generator;
* replay file.

Protocol versions must be isolated in parser/adapter classes.

Unsupported protocol versions must produce diagnostics, not crashes.

## Current domain constraints

Current iteration assumptions:

* SiriusScope works with 5 `BandItem` objects.
* Each `BandItem` represents one BCO band up to 500 MHz.
* 5 bands provide up to 2500 MHz of simultaneous observation.
* Full product frequency range is 0.3–18 GHz.
* Current antenna model uses 2 beams: `beamIndex = 0` and `beamIndex = 1`.
* Future 8-beam support must not be made impossible.
* Input amplitude range is 1–127.
* Amplitude value 0 is invalid for input samples.
* Invalid input data must be rejected or marked diagnostically without crashing.
* Bearing calculation is the main task of the current product iteration.

## Time model rules

Preserve the original BCO `sampleIndex`.

Use a dedicated time model to derive:

* local time from recording start;
* global UTC/system time;
* display time for WaterfallView and result table.

Do not replace `sampleIndex` with UI time.

## UI component responsibilities

`SpectrumView`:

* displays frequency scale;
* displays and controls 5 `BandItem` objects;
* controls the visible frequency range for `WaterfallView`.

`BandItem`:

* represents RPU settings for one frequency band;
* must not directly send hardware commands from QML;
* changes must go through application/controller layer.

`WaterfallView`:

* displays frequency-time history;
* must support preserved history;
* must not be cleared when frequency viewport changes;
* must be prepared for asynchronous data loading.

`AntennaIndicator`:

* displays current antenna azimuth;
* displays selected scan sector;
* displays bearing results by `BandItem` color.

`ResultTable`:

* displays final scan results;
* loads previous saved results on startup;
* is read-only for the user in the current iteration.

`StatusBar`:

* displays program, hardware, recording, and diagnostic status;
* replaces a visible event log in the current iteration.

## Storage rules

SiriusScope must preserve data between launches.

Store large runtime data in files, not in QSettings.

Expected storage responsibilities:

* Waterfall rows;
* result table rows;
* metadata;
* settings;
* technical logs;
* indexes/cache for fast history access.

Use binary files for high-volume data.
Use INI or JSON for settings and metadata.
File I/O must not block the GUI thread.

## Testing rules

Add tests for:

* domain models;
* time conversion;
* protocol parsers;
* invalid input handling;
* storage read/write;
* file rotation;
* bearing-related calculations;
* nontrivial data aggregation.

Target coverage for the project is at least 50%.

For UI-only changes, run the application and manually verify the affected view.

## Build commands

Configure debug build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
```

Build:

```bash
cmake --build build
```

Run tests:

```bash
ctest --test-dir build --output-on-failure
```

Use Qt 6.8+ with Qt Quick.
The target development version is Qt 6.9.3.

## Change discipline

Prefer small, scoped changes.

When implementing a task:

1. Read the relevant docs.
2. Identify the target layer.
3. Avoid unrelated refactoring.
4. Keep public interfaces stable unless the task requires changing them.
5. Add or update tests for business logic.
6. Update docs when behavior, architecture, or constraints change.

Do not silently introduce new product features.
Do not remove simulator compatibility.
Do not weaken real-time responsiveness requirements.
Do not move temporary prototype logic into permanent architecture without documenting it.

