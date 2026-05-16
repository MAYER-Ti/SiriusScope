# Architecture Baseline

This note describes the stage 1 architectural baseline for SiriusScope.

## Layers

SiriusScope keeps the same logical layers defined in `docs/architecture/layers.md`:

- UI: QML views and lightweight bindings only.
- Application: QML-facing models, controllers, commands, and composition root.
- Core: domain models, constraints, validation, and UI-independent operation results.
- Processing: sample validation, aggregation, Waterfall row preparation, and future bearing inputs.
- Hardware adapters: BCO and antenna sources/commands behind stable interfaces.
- Infrastructure: storage, settings, and diagnostics interfaces.

## Added interfaces

Hardware contracts live under `src/hardware/interfaces/`:

- `IBcoSampleSource`
- `IAntennaAzimuthSource`
- `IBcoControl`
- `IAntennaControl`

Infrastructure contracts live under `src/infrastructure/interfaces/`:

- `IWaterfallStorage`
- `IResultTableStorage`
- `ISettingsStorage`
- `IDiagnosticsSink`

These contracts use domain/processing types and standard C++ types. They do not depend on QML or Qt Quick.

## Current stub mode

`ApplicationBootstrap` is the current composition root. It creates the existing demo models and stub controllers:

- `FrequencyViewportModel`
- `FrequencyGridModel`
- `SpectrumControllerStub`
- `SpectrumDecimator`
- `WaterfallControllerStub`
- `AntennaControllerStub`

It also creates placeholder implementations for diagnostics, Waterfall storage, BCO control, and antenna control. These placeholders do not implement protocols, binary storage, or bearing calculation.

## UI boundary

QML must continue to talk only to application-facing singletons and QML elements. It must not create hardware adapters, parse packets, read/write archives, or inspect low-level diagnostics directly.

The current QML singleton names are unchanged. Future simulator and hardware modes should be selected inside `ApplicationBootstrap` or a later application composition service, not inside QML.

## Replacing stub mode later

Future stages should replace placeholders in this order:

1. Add simulator implementations of the hardware interfaces.
2. Route samples through processing instead of synthetic UI timers.
3. Replace null storage with persistent infrastructure implementations.
4. Add real UDP/TCP adapters behind the same hardware interfaces.
5. Connect diagnostics to an application status model and technical log storage.

The UI should not need a separate simulator path or hardware path when these replacements happen.
