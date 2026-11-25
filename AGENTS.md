# Repository Guidelines

## Purpose and Layout

This repository explores C++26 reflection to build a highly automated serialization
library for games. Prefer compile-time reflection and annotations over handwritten
registration, macros, or duplicated serialization boilerplate.

* This repo is under DEV, no forward compatibility guarantees are required.

- `src/srl/`: serialization library, currently centered on `core.hpp`.
- `main.cpp`: executable experiments and usage examples.
- `xmake.lua`: authoritative build configuration; `srl` is the header-only
  library, `srl.example` is the default executable, and `srl.tests` and
  `srl.benchmarks` are opt-in executables.
- `build/`, `.xmake/`, and `compile_commands.json`: generated outputs, not sources
  to edit manually.

## Toolchain and Commands

Use Windows MinGW-w64 with GCC 16 and its matching libstdc++. Ensure the selected
GCC 16 build implements the reflection features used here; a version number alone
does not guarantee support for every experimental feature. Do not silently switch
to MSVC, Clang, an older GCC, or an older language standard to bypass errors.

The xmake environment is expected to already use the repository's GCC 16 MinGW
toolchain. Do not run additional xmake configuration commands or change the
platform, architecture, build mode, or toolchain in an attempt to fix build
errors. Report a missing or incorrect toolchain instead of reconfiguring it.

Run build commands from the repository root in PowerShell:

```powershell
xmake                         # Incremental build of default targets
xmake -b srl.example          # Explicit build of the example
xmake -b -r srl.example       # Force rebuild; -r means rebuild, not run
```

Clean generated build outputs with:

```powershell
xmake clean                   # Remove default targets' build outputs
xmake clean srl.example      # Clean only the example
```

Build successfully before using an existing executable because it may be stale.
`xmake -r` requests a rebuild; it does not run the target.

Keep `set_languages("c++latest")`, `-freflection`, and the `stdc++exp` link dependency
in `xmake.lua` while required by the current GCC 16 implementation. Verify the
compiler actually selected by xmake in verbose output when diagnosing toolchain
issues. Keep C++ exceptions enabled.

## Code Style and Language Features

- Latest C++26 and experimental features are explicitly allowed, including
  `<meta>`, reflection expressions, splicing, annotations, and `template for`.
  Do not replace them merely because an IDE or older compiler rejects them.
- Follow nearby formatting: four-space indentation, opening braces on the same
  line, and `#pragma once` in headers. Library identifiers use `snake_case` and
  belong in `srl` or an appropriate nested namespace.
- Make headers self-contained by including the standard headers they use.
- Prefer concepts, `requires`, `constexpr`, `consteval`, and `static_assert` to
  encode requirements and diagnose invalid schemas at compile time.
- Prefer RAII, value semantics, explicit ownership, and typed views such as
  `std::span`. Avoid unnecessary allocation, runtime type erasure, and abstraction.
- Comments explain non-obvious invariants, wire-format decisions, or compiler
  limitations, not a restatement of the code.

## Type Invariants and Error Handling

- Defensive programming is prohibited: do not repeatedly check established
  internal invariants, add speculative null checks, silently repair invalid
  state, swallow errors, or return fabricated defaults to continue execution.
- Design types so invalid states cannot be represented. Establish invariants at
  construction or through validated factories, restrict mutation, and preserve
  invariants across every public operation, including exception paths.
- Validate untrusted serialized bytes at the input boundary. Bounds, integer
  overflow, offsets, sizes, identifiers, and format versions are real parsing
  requirements, not redundant defensive checks. Internal code should then use
  the validated representation rather than revalidate it at every layer.
- Use C++ exceptions for hard failures that prevent an operation from fulfilling
  its contract. Propagate them to a boundary that can meaningfully handle them;
  do not use `abort`, `exit`, or sentinel values as substitutes.
- Use `std::optional<T>` for expected absence without an error reason,
  `std::expected<T, Error>` for recoverable failures requiring details, and a
  scoped enum error code when no success payload is needed. Errors in external
  data normally belong to this recoverable category.
- Reject invalid compile-time schemas at compile time. Do not defer known schema
  errors to runtime exceptions. Use `noexcept` only when the implementation and
  its callees genuinely guarantee it; never to turn exceptions into termination.

## Serialization and Verification

- Make field IDs, versioning, byte order, ownership, and compatibility rules
  explicit. Do not assume native object layout, padding, pointers, or host
  endianness form a portable serialized representation.
- Keep edits scoped to the requested experiment or library behavior. Do not
  introduce compatibility layers or broad refactors without a concrete need.
- TESTS: only do when user required, For code changes, build and run relevant examples with the configured GCC 16
  toolchain. Add compile-time checks for reflection/schema rules and focused
  runtime tests for round trips and recoverable malformed-input errors as those
  facilities are implemented. Build the non-default GoogleTest target with
  `xmake -b srl.tests`, then run it with `xmake run srl.tests`.
- Report the commands actually run and their outcomes. If an unfinished
  experiment or toolchain issue blocks verification, report it explicitly;
  never claim success based on an old executable.
