# Contributing to Filum

Thank you for your interest in contributing.

## Getting started

```bash
git clone https://github.com/kavishka-dot/filum.git
cd filum
cmake -B build -DFILUM_TARGET=host -DFILUM_ENABLE_ASAN=OFF
cmake --build build
cd build && ctest --output-on-failure
```

All 9 tests must pass before submitting a pull request.

## Code style

- C99. No C++, no VLAs, no `alloca`, no dynamic allocation.
- 4-space indentation. No tabs.
- Every public function must return `FLError`.
- Every public function must have a Doxygen comment with `@param`, `@retval`.
- Every new code path must have a test.

## Making changes

1. Fork the repo and create a branch: `git checkout -b feat/my-feature`
2. Make your changes.
3. Add or update tests in `tests/`.
4. Run the full test suite: `cd build && ctest -V`
5. Run the demo to verify end-to-end behavior: `./build/filum_demo`
6. Open a pull request with a clear description of what changed and why.

## Pull request checklist

- [ ] All 9 tests pass (`ctest --output-on-failure`)
- [ ] New functionality has tests
- [ ] Public API changes have Doxygen comments
- [ ] `CHANGELOG.md` updated under `[Unreleased]`
- [ ] No dynamic memory allocation introduced
- [ ] Compiles clean with `-Wall -Wextra` (no warnings)

## Reporting bugs

Open a GitHub issue with:
- Filum version (`fl_version_string()` output)
- Platform (OS, compiler, MCU if applicable)
- Minimal reproduction case
- Expected vs actual behavior

## Security vulnerabilities

See [SECURITY.md](SECURITY.md). Do not open public issues for security bugs.
