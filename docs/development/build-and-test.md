# Build and Test Guide

This document defines the standard build, run, test, and quality-check workflow for SiriusScope.

Use this file when changing CMake, dependencies, tests, CI, or developer tooling.

## 1. Target environment

SiriusScope is a Qt/C++ desktop application.

Target stack:

- C++20;
- Boost;
- Qt 6;
- Qt Quick / QML;
- CMake;
- Conan;
- CTest;
- Qt Test or Catch2 for unit tests.

Target development version:

```text
Qt 6.9.3
```

Minimum acceptable Qt version for development may be:

```text
Qt 6.8+
```

provided that the code remains compatible with the target Qt version and required Qt Quick features.

## 2. Expected repository layout

Recommended build and test related layout:

```text
/
├── CMakeLists.txt
├── src/
│   ├── app/
│   └── ui/
├── tests/
│   ├── CMakeLists.txt
│   ├── domain/
│   ├── processing/
│   ├── infrastructure/
│   └── hardware/
├── docs/
└── build/
```

Rules:

* keep generated files in `build/`;
* do not commit local build directories;
* do not commit generated CMake cache files;
* do not commit temporary IDE output unless explicitly required.

## 3. Configure

### Debug build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
```

### Release build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

### Multi-config generators

For generators such as Visual Studio or Ninja Multi-Config:

```bash
cmake -S . -B build
```

Then specify configuration at build time:

```bash
cmake --build build --config Debug
cmake --build build --config Release
```

## 4. Build

### Default build

```bash
cmake --build build
```

### Parallel build

```bash
cmake --build build -j
```

### Release build with multi-config generator

```bash
cmake --build build --config Release
```

## 5. Run

The current executable target is expected to be:

```text
appSiriusScope
```

The exact runtime path depends on generator and platform.

Typical Linux path:

```bash
./build/appSiriusScope
```

For multi-config generators, the executable may be under a configuration subdirectory, for example:

```bash
./build/Debug/appSiriusScope
./build/Release/appSiriusScope
```

If the executable location differs, inspect the CMake build output or target properties.

## 6. Tests

### Run all tests

```bash
ctest --test-dir build --output-on-failure
```

### Run tests with verbose output

```bash
ctest --test-dir build --output-on-failure --verbose
```

### Run tests for a specific configuration

For multi-config generators:

```bash
ctest --test-dir build -C Debug --output-on-failure
ctest --test-dir build -C Release --output-on-failure
```

## 7. Test framework

Preferred frameworks:

* Qt Test;
* Catch2.

Use whichever framework is already present in the repository for nearby tests.

If no test framework exists yet, prefer starting with small C++ tests for domain and processing logic before UI tests.

## 8. What must be tested

Add or update automated tests when changing nontrivial logic in:

* domain models;
* time conversion;
* frequency calculations;
* amplitude validation;
* beam handling;
* bearing-related calculations;
* protocol parsers;
* sample aggregation;
* Waterfall row building;
* binary storage;
* result-table storage;
* settings loading and defaults;
* file rotation;
* simulator behavior.

Target project coverage:

```text
>= 50%
```

Coverage does not need to be solved in one task, but new business logic should not be added without tests when it is reasonably testable.

## 9. Minimum test expectations by change type

### Documentation-only change

Required:

* no build required unless documentation is referenced by CMake or resources.

Recommended:

* check links and paths manually.

### QML-only visual change

Required:

* run the application;
* manually verify the affected screen or component.

Recommended:

* verify startup;
* verify layout at expected minimum window size;
* check console for QML errors.

### C++ domain/model change

Required:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Also required:

* add or update unit tests for changed behavior.

### Protocol/parser change

Required:

* parser unit tests;
* invalid-packet tests;
* unsupported-version tests;
* build and CTest run.

### Storage change

Required:

* write/read tests;
* corrupted or missing file tests when applicable;
* rotation tests if rotation behavior is affected;
* build and CTest run.

### Processing or bearing change

Required:

* deterministic unit tests;
* edge-case tests;
* invalid input tests;
* build and CTest run.

### Threading or queue change

Required:

* build and CTest run;
* tests for queue bounds or shutdown behavior where practical;
* manual run to check no UI freeze.

### CMake or dependency change

Required:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Recommended:

* also test Release configuration if the change affects compiler flags, optimization, packaging, or target properties.

## 10. UI manual smoke test

After UI-related changes, manually check:

1. Application starts without QML errors.
2. `MainWindow` appears.
3. `SpectrumView` is visible.
4. `WaterfallView` is visible.
5. `AntennaIndicator` is visible.
6. Footer / `StatusBar` is visible.
7. Basic interaction does not freeze the UI.
8. Test/simulator mode, if available, still works.
9. No repeated warnings appear in the console.

For `SpectrumView` changes, also check:

* zoom by mouse wheel;
* horizontal pan behavior;
* 5 `BandItem` objects remain visible/available;
* frequency labels remain readable.

For `AntennaIndicator` changes, also check:

* current azimuth display;
* test-mode antenna movement if available;
* bearing marks if simulated data is available.

For `WaterfallView` changes, also check:

* redraw behavior;
* viewport synchronization with `SpectrumView`;
* no clearing of history unless explicitly requested by the feature.

## 11. Recommended test naming

Use clear names that identify the tested component.

Examples:

```text
tst_frequencyviewportmodel.cpp
tst_timebase.cpp
tst_samplevalidator.cpp
tst_protocolparser_v1.cpp
tst_waterfallrowbuilder.cpp
tst_bearingservice.cpp
tst_binarywaterfallstorage.cpp
```

or:

```text
test_frequency_viewport_model.cpp
test_timebase.cpp
test_sample_validator.cpp
```

Prefer consistency with existing repository style.

## 12. Recommended CMake test pattern

A typical test target may look like:

```cmake
add_executable(tst_timebase
    tst_timebase.cpp
)

target_link_libraries(tst_timebase
    PRIVATE
        Qt6::Test
        sirius_core
)

add_test(NAME tst_timebase COMMAND tst_timebase)
```

If the project does not yet have separated libraries such as `sirius_core`, prefer creating testable libraries rather than testing only through the final GUI executable.

## 13. Sanitizers

When practical, use sanitizers for C++ logic.

Recommended debug options:

* AddressSanitizer;
* UndefinedBehaviorSanitizer.

Example CMake configure command may be added later when sanitizer options are formalized:

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DSIRIUS_ENABLE_ASAN=ON
```

Do not add sanitizer flags ad hoc to unrelated files. Prefer centralized CMake options.

## 14. Formatting

Use a consistent style for C++ and QML.

Preferred tools:

* `clang-format` for C++;
* `clang-tidy` for static checks where configured;
* QML formatting through the IDE or agreed project formatter.

Do not reformat large unrelated files in a feature task.

Formatting-only changes should be separate from behavior changes.

## 15. Static analysis

When configured, run:

```bash
clang-tidy
```

or the project-provided static-analysis target.

If no static-analysis target exists yet, do not invent one inside an unrelated task. Add tooling in a dedicated development/tooling task.

## 16. Build artifacts

Do not commit:

```text
build/
CMakeCache.txt
CMakeFiles/
cmake_install.cmake
compile_commands.json
*.user
*.autosave
```

`compile_commands.json` may be generated locally for IDE tooling. Commit it only if the project explicitly decides to track it.

## 17. Dependency policy

When adding a dependency:

1. Prefer standard C++ or Qt already available in the project.
2. Justify why the dependency is needed.
3. Keep the dependency out of domain models unless unavoidable.
4. Update build documentation.
5. Update CI or setup instructions if applicable.

Do not add large dependencies for small utilities.

## 18. Performance checks

For performance-sensitive changes, especially Waterfall, processing, buffering, or storage:

Check at least:

* no obvious UI freeze;
* no unbounded memory growth;
* no heavy processing in QML;
* no blocking file I/O on GUI thread;
* bounded or controlled queues for high-rate data;
* diagnostic messages for overload conditions where applicable.

Expected long-term performance targets are defined in scope and subsystem documents.

## 19. Reporting completion

When reporting completion of a code task, include:

* what changed;
* files changed;
* build command used;
* tests run;
* any tests not run and why;
* known limitations or follow-up tasks.

Example:

```text
Implemented TimeBase conversion and tests.

Changed:
- src/core/timebase.h
- src/core/timebase.cpp
- tests/domain/tst_timebase.cpp

Checked:
- cmake --build build
- ctest --test-dir build --output-on-failure

Notes:
- Protocol timestamp integration remains TBD.
```

## 20. Codex-specific rules

When Codex works on the repository:

* read `AGENTS.md` first;
* read `docs/README.md`;
* read only task-relevant documents;
* avoid unrelated refactoring;
* keep changes small;
* add tests for nontrivial C++ logic;
* do not move heavy logic into QML;
* do not bypass hardware/simulator abstraction;
* do not silently implement future extensions;
* update docs when module contracts or behavior change.

