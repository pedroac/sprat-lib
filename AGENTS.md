# AGENTS.md

Guidance for AI agents (and humans) working on **sprat-lib**.

## Purpose

sprat-lib is a C library that loads sprite sheet metadata produced by
[sprat-cli](https://github.com/pedroac/sprat-cli) (`spratconvert --transform raw`) into games and engines.

The raw format is defined by sprat-cli's `transforms/raw.jsonnet` (its header
comment is the specification). When that transform changes, update `src/sheet.c`,
the public model in `include/sprat/sprat.h`, and the fixture in `tests/fixtures/`.
A breaking change to the format bumps the `sprat-raw` version on its first line.

## Priorities

1. **Never crash or read out of bounds on bad input.** Metadata files are
   untrusted. Validate every index, size and enum value at load time, so code
   that uses a loaded sheet needs no checks of its own.
2. **Simple, stable C API.** Keep it to plain structs, `sprat_` prefixes, and no
   hidden global state. The one global is the optional memory functions set with
   `sprat_set_memory_functions()`, modelled on SDL3, so callers never pass an
   allocator around.
3. **Portable C99 with no dependencies.** It must build with GCC, Clang and MSVC.

## Code layout

- `include/sprat/sprat.h`: the only public header.
- `src/memory.c`: the memory functions (`sprat_set_memory_functions`).
- `src/error.c`: filling in a `sprat_error`, shared by the loader and the binder.
- `src/bind.c`: `sprat_sheet_bind()`, names to indexes with messages that suggest fixes.
- `src/reader.c`: tokenizer for the raw format (integers, numbers, length-prefixed
  UTF-8 strings copied into the sheet's string pool) with line/column errors.
- `src/sheet.c`: maps raw records onto the model, plus the loaders and queries.
- `tests/`: C tests registered with CTest. `tests/fixtures/` holds real spratconvert output.
  - `test_reader.c`: the tokenizer. `test_sheet.c`: what records become in the model.
    `test_errors.c`: every error's exact message and position, plus sweeps that cut the
    fixture at every byte, replace every token, and corrupt it at random.
    `test_file.c`: `sprat_sheet_load_file()`. `test_api.c`: the queries and helpers.
    `test_bind.c`: `sprat_sheet_bind()`.
    `test_drawing.c`: the README's drawing recipe.
  - `test.h` checks print what they saw (`CHECK_INT`, `CHECK_STR`, ...); `support.h` has the
    counting allocator and `expect_error(text, status, line, column, message)`.
  - A new or changed error message goes in `test_errors.c`'s table, exactly.
- `examples/sprat_dump.c`: prints a loaded sheet.

## Coding practices

- C99. No VLAs, no compiler extensions in the public header.
- Every allocation goes through the memory functions (`malloc`, `calloc`,
  `realloc`, `free` by default). A load takes one snapshot of them, and the sheet
  keeps its free function for `sprat_sheet_free()`.
- A sheet is one block sized from the totals on its `sheet` line; records take
  slices of it. Don't add per-record allocations.
- A failing load must free everything it allocated. `test_out_of_memory` checks
  this by failing each allocation in turn.
- Keep the build warning-free with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`.

## Build and test

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DSPRAT_SANITIZE=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Add or update tests for every behavior change or bug fix.

## Versioning and commits

- Update `VERSION` (`vMAJOR.MINOR.PATCH`) in the same commit as the change.
- Commit messages: `feat: ...`, `fix: ...`, `docs: ...`, `test: ...`, `refactor: ...`.
- Never commit build output.
