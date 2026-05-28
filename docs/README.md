# SiriusScope Documentation

This directory contains the project documentation used by developers and AI assistants such as Codex.

The purpose of this documentation set is to keep the repository maintainable over time: `AGENTS.md` defines how to work with the repository, while the files in `docs/` define what SiriusScope is, how it is structured, and how specific subsystems must behave.

SiriusScope is documented as a real high-load BCO stream processing system, not as an
MVP/demo GUI application. The documentation separates the C++ data plane from the
Qt/QML/Application control plane. Older demo-rate wording must be read as current/legacy
implementation unless it is explicitly marked as target architecture.

## Documentation map

### Product scope

Read these documents to understand what SiriusScope must do in the current iteration.

- `spec/scope.md` — current product scope, in-scope features, out-of-scope features, and high-level constraints.
- `spec/glossary.md` — project terms and abbreviations.
- `spec/SiriusScope_TZ_v0.1.md` — full technical assignment for SiriusScope. Use as the detailed source of requirements.

Read `spec/SiriusScope_TZ_v0.1.md` when:

- starting a large task;
- generating or changing architecture;
- creating new subsystems;
- requirements are unclear or disputed;
- changing requirements;
- reviewing whether implementation matches the technical assignment.

### Architecture

Read these documents before changing module boundaries, dependencies, threading, data flow, or cross-layer communication.

- `architecture/layers.md` — architectural layers, responsibilities, allowed dependencies, and forbidden dependencies.
- `architecture/data-flow.md` — runtime flow of signal data, azimuth data, bearing results, UI updates, and storage operations.
- `architecture/baseline.md` — current stage 1 composition root and hardware/infrastructure interface baseline.
- `architecture/high-load-data-plane.md` — target data plane, control plane, ingest pipeline, DSP pipeline, snapshots, backpressure, memory pool, bounded queue rules, simulator profiles, and migration milestones.

### Domain model

Read these documents before changing signal models, frequency models, time conversion, bearing calculation, or waterfall color logic.

These documents are planned but do not exist yet. Until they are added, use `spec/SiriusScope_TZ_v0.1.md`, `spec/scope.md`, and the architecture documents for the relevant requirements.

- `domain/models.md` — core domain entities, constraints, and formulas.
- `domain/timebase.md` — time model based on BCO `sampleIndex`, local time, and global time.
- `domain/bearing.md` — bearing calculation concepts and current assumptions.
- `domain/waterfall-color-model.md` — amplitude and two-beam directional color model for `WaterfallView`.

### User interface

Read these documents before changing QML components, UI layout, user interaction, or visual behavior.

These documents are planned but do not exist yet. Until they are added, use `spec/SiriusScope_TZ_v0.1.md`, `spec/scope.md`, and the existing QML/code context for the relevant requirements.

- `ui/components.md` — responsibilities of `SpectrumView`, `BandItem`, `WaterfallView`, `AntennaIndicator`, `ResultTable`, and `StatusBar`.
- `ui/spectrum-view.md` — detailed behavior of `SpectrumView` and `BandItem`.
- `ui/waterfall-view.md` — detailed behavior of the waterfall history view.
- `ui/antenna-indicator.md` — sector selection, antenna azimuth display, and bearing display.
- `ui/result-table.md` — final scan result table behavior.
- `ui/status-bar.md` — status and diagnostic display rules.

### Hardware and simulator

Read these documents before changing UDP/TCP communication, protocol parsers, hardware control, simulator behavior, ingest pipeline behavior, or simulator load profiles.

These documents are planned but do not exist yet. Until they are added, use `spec/SiriusScope_TZ_v0.1.md`, `spec/scope.md`, and the architecture documents for the relevant requirements.

- `hardware/interfaces-and-protocols.md` — hardware adapter boundaries, protocol-version rules, and simulator compatibility.
- `hardware/bco-udp-protocol.md` — BCO UDP data stream format. May contain `TBD` sections until the real protocol is finalized.
- `hardware/bco-control-protocol.md` — BCO control protocol for reception ranges, dwell time, filters, polarization, attenuators, diagnostics, and BCO-side RPU control. Planned; use `TBD` sections until the protocol is provided.
- `hardware/antenna-tcp-protocol.md` — antenna / rotating device TCP messages. May contain `TBD` sections until the real protocol is finalized.
- `hardware/simulator.md` — requirements for the software simulator, including `UiDemo`, `MediumLoad`, `RealBcoEquivalent`, and `Stress150Percent` profiles.

### Storage

Read these documents before changing recording, archive loading, metadata, settings, logs, file rotation, storage backpressure, or raw/near-raw stream persistence.

These documents are planned but do not exist yet. Until they are added, use `spec/SiriusScope_TZ_v0.1.md`, `spec/scope.md`, and the architecture documents for the relevant requirements.

- `storage/archive-format.md` — archive directory layout, binary storage responsibilities, metadata, and rotation principles.
- `storage/settings.md` — application settings format and defaults.
- `storage/logging.md` — technical log requirements.

### Development

Read these documents before changing build scripts, tests, formatting, or developer workflow.

- `development/development-roadmap.md` — strategic roadmap for moving SiriusScope toward the target high-load data plane architecture.
- `development/build-and-test.md` — build, test, and QA commands.
- `development/doxygen.md` - правила Doxygen-документирования исходного API и процесс генерации.
- `generate-doxygen.ps1` - генерация русскоязычной HTML-документации Doxygen.
- `development/conan-migration-task.md` — Conan-first migration requirements and completion criteria.
- `development/coding-style.md` — C++ and QML style rules.
- `development/testing-strategy.md` — required test types and coverage expectations.
- `development/codex-task-template.md` — recommended format for tasks given to Codex.

Read `development/development-roadmap.md` before large tasks that affect architecture, application flow, storage, simulator, hardware adapters, scanning, or bearing.

`development/coding-style.md`, `development/testing-strategy.md`, and `development/codex-task-template.md` are planned but do not exist yet. Until they are added, use `development/build-and-test.md`, `AGENTS.md`, and the style of nearby code.

## Source of truth priority

If documentation files conflict, use this priority:

1. Current user task.
2. Technical assignment and current scope documents in `docs/spec/`.
3. Architecture and domain documents.
4. Existing code.
5. Root `README.md`.

When a conflict is discovered, do not silently choose one version and continue. Mention the conflict in the implementation notes or update the documentation as part of the task.

## Current documentation status

This documentation set is intended to be built incrementally.

Some files may initially contain only the current assumptions and `TBD` sections. That is acceptable. A short, explicit document with known gaps is better than hidden assumptions in code or QML.

Files listed above outside `spec/`, `architecture/`, and `development/build-and-test.md` may be planned documents that have not been created yet. Missing planned documents are not a blocker by themselves; use the existing source-of-truth priority and mention the missing document if it affects the task.

## Rules for adding documentation

When adding or editing documentation:

- keep each file focused on one topic;
- avoid duplicating large parts of the technical assignment;
- link to related documents instead of copying their contents;
- mark unknown protocol details as `TBD`;
- separate current-version requirements from future extensions;
- separate target high-load architecture from legacy/current MVP implementation paths;
- never describe Qt/QML, `WaterfallController`, `SignalSampleBus`, or `BearingFrameBus` as valid high-load raw stream transports;
- use `data plane`, `control plane`, `ingest pipeline`, `DSP pipeline`, `snapshot`, `backpressure`, `memory pool`, and `bounded queue` consistently;
- обновляйте Doxygen-комментарии при изменении публичных C++-контрактов;
- update `docs/README.md` when adding, renaming, or removing documentation files.

## Rules for Codex

For Codex-assisted work:

1. Start with `AGENTS.md`.
2. Open `docs/README.md`.
3. Read only the documents relevant to the current task.
4. Do not implement features that are marked out of scope.
5. Do not change architecture rules unless the task explicitly asks for an architectural change.
6. Update documentation when code behavior, module boundaries, or subsystem contracts change.
