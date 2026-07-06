[![Tests](https://github.com/inlay-cloud/inlay-juce-module/actions/workflows/ci.yml/badge.svg)](https://github.com/inlay-cloud/inlay-juce-module/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

# Inlay JUCE Module Repository

This repository contains the Inlay licensing JUCE module, example projects, and
local test harnesses used while developing the module.

## Repository Structure

```text
.
|-- modules/
|   `-- inlay_product_unlocking/     # Main Inlay licensing JUCE module
|-- examples/
|   `-- ArpeggiatorTutorial/         # Demo plugin used for manual testing
|-- tests/
|   `-- UnitTestsRunner/             # JUCE unit test runner entry point
|-- CMakeLists.txt                   # CMake build for examples and tests
`-- Makefile                         # Projucer, demo build, and packaging helpers
```

## Main Module

See: [`docs/inlay_product_unlocking.md`](docs/inlay_product_unlocking.md).

## Examples

`examples/ArpeggiatorTutorial` is a standalone JUCE demo plugin that depends on
the module via a relative module path. Use it for manual activation-flow testing
and development iteration.

## Tests

The root CMake project builds `InlayProductUnlockingTests`, a console test runner
that links against the module with `JUCE_UNIT_TESTS=1`.

## JUCE Dependency

Inlay uses JUCE to build its examples and tests, but consuming projects remain
in control of their own JUCE dependency.

When this repository is built as the top-level CMake project, the build/test target
fetches the pinned JUCE commit automatically.

When adding this repository to another JUCE project, provide JUCE before adding
Inlay:

```cmake
add_subdirectory(third_party/JUCE)
add_subdirectory(third_party/inlay-juce-module EXCLUDE_FROM_ALL)

target_link_libraries(MyPlugin PRIVATE
    inlay::inlay_product_unlocking
)
```

Inlay detects the existing `juce::juce_core` target and does not fetch another
JUCE copy. This repository does not include JUCE as a git submodule.

## License

This repository is licensed under the MIT License. See [LICENSE](LICENSE).
