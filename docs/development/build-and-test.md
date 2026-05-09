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
Qt 6.10.1
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
    ├── build-codex/
    ├── build-debug/
    └── build-release/
```

Rules:

* keep generated files under `build/`;
* create concrete CMake build trees as subdirectories of `build/`, not as the repository-root `build` directory itself;
* use `build/build-codex` for Codex-assisted local verification unless a task requires a different build tree;
* do not commit local build directories;
* do not commit generated CMake cache files;
* do not commit temporary IDE output unless explicitly required.

The repository-root `build/` directory is an umbrella directory for generated build trees. It may contain IDE-generated folders or stale partial configuration files. Do not assume `build/` itself has a valid `CMakeCache.txt`.

## 3. Configure

Default local verification build directory:

```text
build/build-codex
```

Conan-first workflow is mandatory for all supported local and CI builds. Direct configure without Conan is a legacy-only fallback for emergency diagnostics and is not supported for regular development.

### Debug build (mandatory Conan-first)

```bash
conan remote remove conancenter || true
conan remote add conancenter https://center2.conan.io
conan install . -of build/build-codex/conan -pr:h conan/profiles/linux-gcc-debug -pr:b conan/profiles/linux-gcc-debug --build=missing
cmake --preset conan-debug
```

The provided Conan profiles in `conan/profiles/` define `compiler.cppstd=gnu20` to satisfy Qt package validation on ConanCenter.

For reproducible dependency graphs, generate/update lockfile when updating dependencies:

```bash
conan lock create . -of build/build-codex/conan -pr:h conan/profiles/linux-gcc-debug -pr:b conan/profiles/linux-gcc-debug --lockfile-out=conan.lock
```

The repository root `conanfile.py` is the Conan entry point and the only supported place for external dependency declaration.

### Release build

```bash
conan install . -of build/build-release/conan -pr:h conan/profiles/linux-gcc-release -pr:b conan/profiles/linux-gcc-release --build=missing
cmake --preset conan-release
```

### Multi-config generators

For generators such as Visual Studio or Ninja Multi-Config:

```bash
cmake -S . -B build/build-codex
```

Then specify configuration at build time:

```bash
cmake --build build/build-codex --config Debug
cmake --build build/build-codex --config Release
```

If CMake reports `could not load cache`, the selected build tree is not configured. Run the configure command for that exact build directory before building.

If CMake or Ninja fails during compiler detection because temporary files under `CMakeFiles/CMakeScratch` cannot be removed, retry with a fresh subdirectory under `build/`, for example `build/build-codex`. Do not create ad hoc build directories at repository root.

## 4. Build

### Default build

```bash
cmake --build build/build-codex
```

### Parallel build

```bash
cmake --build build/build-codex -j
```

### Release build with multi-config generator

```bash
cmake --build build/build-codex --config Release
```

## 5. Run

The current executable target is expected to be:

```text
appSiriusScope
```

The exact runtime path depends on generator and platform.

Typical Linux path:

```bash
./build/build-codex/appSiriusScope
```

For multi-config generators, the executable may be under a configuration subdirectory, for example:

```bash
./build/build-codex/Debug/appSiriusScope
./build/build-codex/Release/appSiriusScope
```

On Windows with the current Ninja/MSYS-style build, the executable is expected at:

```text
build/build-codex/appSiriusScope.exe
```

If the executable location differs, inspect the CMake build output or target properties.

## 6. Tests

### Run all tests

```bash
ctest --test-dir build/build-codex --output-on-failure
```

### Run tests with verbose output

```bash
ctest --test-dir build/build-codex --output-on-failure --verbose
```

### Run tests for a specific configuration

For multi-config generators:

```bash
ctest --test-dir build/build-codex -C Debug --output-on-failure
ctest --test-dir build/build-codex -C Release --output-on-failure
```

If CTest prints `No tests were found!!!`, the command itself succeeded but the current project configuration does not define test targets yet.

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
cmake --build build/build-codex
ctest --test-dir build/build-codex --output-on-failure
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
cmake -S . -B build/build-codex -DCMAKE_BUILD_TYPE=Debug
cmake --build build/build-codex
ctest --test-dir build/build-codex --output-on-failure
```

For Conan-related changes or after third-party dependencies are introduced, also verify:

```bash
conan install . -of build/build-codex/conan -s build_type=Debug --build=missing
cmake -S . -B build/build-codex -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=build/build-codex/conan/conan_toolchain.cmake
cmake --build build/build-codex
ctest --test-dir build/build-codex --output-on-failure
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
cmake -S . -B build/build-asan -DCMAKE_BUILD_TYPE=Debug -DSIRIUS_ENABLE_ASAN=ON
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
3. Add third-party dependencies through Conan.
4. Keep the dependency out of domain models unless unavoidable.
5. Update build documentation.
6. Update CI or setup instructions if applicable.

Do not add large dependencies for small utilities.

Boost is part of the target development stack, but it must not be linked to a target before code actually uses it. On first real Boost usage:

* pin a concrete Boost version in `conanfile.py`, for example `requires = "boost/<version>"`;
* configure through `conan install` and `CMAKE_TOOLCHAIN_FILE`;
* use `find_package(Boost REQUIRED COMPONENTS ...)` in CMake;
* link only the required Boost components to the specific target that needs them.

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
- cmake --build build/build-codex
- ctest --test-dir build/build-codex --output-on-failure

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



## 10. Troubleshooting (Conan/CMake)

- `Could not find package configuration file provided by Qt6`: run `conan install` for the same profile/build type and configure via the preset with Conan toolchain.
- `CMAKE_TOOLCHAIN_FILE` points to missing file: verify `-of` path and preset path alignment (`build/.../conan/build/<Config>/generators/conan_toolchain.cmake`).
- Dependency drift between machines: regenerate and commit `conan.lock` after intentional dependency updates.
- CI fails guard step: ensure configure uses `cmake --preset conan-debug` and cache contains `conan_toolchain.cmake`.
