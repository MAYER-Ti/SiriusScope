# Doxygen Documentation Guide

This document defines how SiriusScope uses Doxygen for source-level API documentation.

## Purpose

Doxygen comments describe public C++ contracts that are easier to understand near the code:

- domain model invariants;
- validation rules and diagnostic codes;
- units for frequencies, time values, azimuths, amplitudes, and quality values;
- ownership of processing outputs such as Waterfall rows and bearing input frames;
- QML-facing application models and signals.

Project requirements, architecture rules, and product scope remain in `docs/`. Do not copy large parts of the technical assignment into Doxygen comments. Link or summarize the relevant contract instead.

## Documentation Scope

Document these source-level elements:

- public headers in `src/core/`;
- public processing contracts in `src/processing/`;
- QML-facing C++ classes in `src/app/`;
- enums, structs, public methods, signals, slots, and non-obvious public fields.

Private helpers may be documented when they encode a domain rule or a nontrivial algorithm. Routine implementation details should stay undocumented or use a short regular comment when needed.

QML files are documented through project UI documents unless a future task introduces a QML documentation workflow.

## Comment Style

Use Doxygen comments in public headers:

```cpp
/*!
 * \brief Creates a validated signal sample.
 *
 * \param[in] beamSample Beam-local input sample.
 * \param[in] bandConfig Band used to derive absolute frequency.
 * \return Created sample or validation issues.
 */
static DomainResult<SignalSample> create(const BeamSample& beamSample,
                                         const BandConfig& bandConfig);
```

Use `//!` for short field or enum descriptions:

```cpp
//! Original BCO sample index. It must be preserved by higher layers.
std::uint64_t sampleIndex = 0;
```

Minimum expectations for public API comments:

- include `\brief` for files, classes, structs, and public functions;
- include `\param[in]`, `\param[out]`, or `\param[in,out]` for meaningful parameters;
- include `\return` when a function returns a value;
- mention units explicitly, for example hertz, nanoseconds, or degrees;
- mention whether invalid input is rejected, diagnosed, or preserved;
- keep comments behavior-focused, not implementation-focused.

## Layer Rules

Doxygen comments must reinforce the architecture:

- core/domain comments must not reference QML item state or concrete hardware protocols;
- processing comments must describe UI-independent inputs and outputs;
- application comments may describe QML-facing properties, signals, slots, and controller responsibilities;
- hardware, infrastructure, and simulator comments must describe boundaries through interfaces when those layers are added.

If a Doxygen comment needs to explain a broader rule, put the full rule in the relevant project document and keep the source comment concise.

## Generating HTML

Doxygen output is a generated artifact and must stay under `build/`.

From the repository root:

```bash
doxygen docs/Doxyfile
```

Generated HTML:

```text
build/docs/doxygen/html/index.html
```

Graphviz is optional. If installed, a future task may enable diagrams in `docs/Doxyfile`.

## Verification

For documentation-only changes:

- run `doxygen docs/Doxyfile` when Doxygen is installed;
- check that warnings point only to intentionally undocumented internal details;
- no C++ build is required unless comments changed generated files, build scripts, or public declarations.

For code tasks that change public C++ contracts:

- update nearby Doxygen comments in the same change;
- keep generated Doxygen output uncommitted;
- update `docs/README.md` when adding or moving documentation pages.
