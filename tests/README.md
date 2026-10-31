# Modeset regression tests

Run the suite from a checkout with Git, Bash, Python 3, and `cc` supporting
AddressSanitizer and UndefinedBehaviorSanitizer (for example, GCC on Ubuntu):

```sh
git fetch --no-tags --depth=1 origin 61dcc93722ecb418bb5f2e00923f05b4b8051dd1
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
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 python3 tests/hw-head-assignment.py
```

The comparison requires base commit `61dcc93722ecb418bb5f2e00923f05b4b8051dd1` in the local Git object database.

The `Modeset regressions` GitHub Actions workflow runs all six suites on PRs,
pushes to `main`, and manual dispatch. It fetches only the pinned comparison
commit in addition to the checkout, so a shallow checkout works. CI uses the
same sanitizer settings as the local runner and has a ten-minute job timeout.

These fixtures and Python runners were brought in from the supporting test
commit `d6ce47a2296f2f900516d4dd7ad1d67e3a26ca4d` for fork PR #2. They now live
alongside the production sources so changes to either can be reviewed and
validated together.

The Python runners extract production functions from the checkout and compile them between the C model headers and scenario files in `fixtures/`. The fixtures are parts of generated translation units, not independently built programs. Keeping the C in separate files makes its types, assertions, and method recording directly reviewable without embedding source in Python strings. The stress and comparison runners share `allocation-model.h`. `hw-head-assignment.py` compiles `AssignProposedHwHeadsOneDisp()` and its helpers from `nvkms-modeset.c` next to the 615.71.09 version and runs both from new, kept and unchanged api-head assignments, with `hw-head-model.h` standing in for the DP library queries and the per-hardware-head state.

These tests cover a simplified allocator and hardware-method model. They do not load a driver, change displays, run RM, or establish hardware timing and firmware behavior.
