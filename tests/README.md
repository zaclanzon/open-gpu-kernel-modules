# Modeset regression tests

Run the suite from a checkout with Git, Bash, Python 3, and `cc` supporting
AddressSanitizer and UndefinedBehaviorSanitizer (for example, GCC on Ubuntu):

```sh
git fetch --no-tags --depth=1 origin e4a5faa2567f28c8eabe0ebb6422b6d0abcf37eb
bash tests/run.sh
```

The runner stops with a nonzero exit status if compilation, an assertion, or a
sanitizer check fails. It defaults to `ASAN_OPTIONS=detect_leaks=0` and
`UBSAN_OPTIONS=halt_on_error=1`; either can be overridden in the environment.
No kernel build, GPU, driver installation, or elevated privileges are required.

To run individual suites:

```sh
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 python3 tests/phywin-allocation.py
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 python3 tests/phywin-sequencing.py
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 python3 tests/window-ownership.py
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 python3 tests/phywin-stress.py
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 python3 tests/ordinary-configurations.py
```

The comparison requires base commit `e4a5faa2567f28c8eabe0ebb6422b6d0abcf37eb` in the local Git object database.

The `Modeset regressions` GitHub Actions workflow runs all five suites on PRs,
pushes to `main`, and manual dispatch. It fetches only the pinned comparison
commit in addition to the checkout, so a shallow checkout works. CI uses the
same sanitizer settings as the local runner and has a ten-minute job timeout.

These fixtures and Python runners were brought in from the supporting test
commit `d6ce47a2296f2f900516d4dd7ad1d67e3a26ca4d` for fork PR #2. They now live
alongside the production sources so changes to either can be reviewed and
validated together.

The Python runners extract production functions from the checkout and compile them between the C model headers and scenario files in `fixtures/`. The fixtures are parts of generated translation units, not independently built programs. Keeping the C in separate files makes its types, assertions, and method recording directly reviewable without embedding source in Python strings. The stress and comparison runners share `allocation-model.h`.

These tests cover a simplified allocator and hardware-method model. They do not load a driver, change displays, run RM, or establish hardware timing and firmware behavior.
