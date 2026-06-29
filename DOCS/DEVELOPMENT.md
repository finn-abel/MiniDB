# Development

## Build Targets

```sh
make
make run
make test
make test-asan
make fmt
make fmt-check
make analyze
make check
make dist
make package
make release
make install
make uninstall
make clean-dist
make clean
```

`make test` builds each test executable listed in `TEST_SRC`, runs the full
suite, and then calls `make clean`.

`make check` is the broad local verification target. It runs:

- `make fmt-check`
- `make analyze`
- `make test`
- `make test-asan`

## Formatting

Formatting is controlled by `.clang-format`.

```sh
make fmt
make fmt-check
```

`make fmt` rewrites source, header, and test files under `src/`, `include/`,
and `tests/`. `make fmt-check` verifies formatting without changing files.

## Static Analysis

```sh
make analyze
```

The analyzer target prefers `clang --analyze` when `clang` is available. If
`clang` is not found and `cppcheck` is installed, it falls back to `cppcheck`.
If neither tool is available, the target prints a skip message.

## Sanitizers

```sh
make test-asan
```

This forces a clean rebuild and runs the test suite with AddressSanitizer and
UndefinedBehaviorSanitizer enabled:

```text
-fsanitize=address,undefined -fno-omit-frame-pointer
```

## Adding Source Files

When adding implementation files, update `SRC` in the `Makefile`. When adding
tests, update `TEST_SRC`.

Tests must not link against `src/main.c`; each test file has its own `main`
function. The Makefile handles this with `TEST_SUPPORT_SRC`, which filters
`src/main.c` out of the support objects used for test executables.

## CI Expectation

Continuous integration should run `make check` from a clean checkout. That
keeps local verification and CI aligned.

## Release Archives

The project version is `0.1.2`, stored in `VERSION` and mirrored by the
Makefile's `VERSION` variable.

```sh
make release
```

This runs the full check suite and creates both release artifacts.

```sh
make dist
```

This creates:

```text
dist/MiniDB-0.1.2.tar.gz
```

The archive contains source, headers, tests, documentation, CI workflow,
formatter config, and the MIT license. Generated archives are ignored by git.

```sh
make package
```

This creates a runnable binary archive for the current system:

```text
dist/MiniDB-0.1.2-<system>-<machine>.tar.gz
```

The binary archive contains `bin/MiniDB`, docs, license, version metadata, and
an `install.sh` helper. It can be extracted and installed on a compatible
machine without compiling from source; after installation, users can run
`MiniDB --version` directly from the terminal.

To remove release archives:

```sh
make clean-dist
```
