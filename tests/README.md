# Modeset regression tests

Run these scripts from a checkout with Python 3 and a C compiler supporting AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 python3 tests/phywin-allocation.py
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 python3 tests/phywin-sequencing.py
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 python3 tests/window-ownership.py
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 python3 tests/phywin-stress.py
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 python3 tests/ordinary-configurations.py
```

The comparison requires base commit `e4a5faa2567f28c8eabe0ebb6422b6d0abcf37eb` in the local Git object database.

The Python runners extract production functions from the checkout and compile them between the C model headers and scenario files in `fixtures/`. The fixtures are parts of generated translation units, not independently built programs. Keeping the C in separate files makes its types, assertions, and method recording directly reviewable without embedding source in Python strings. The stress and comparison runners share `allocation-model.h`.

These tests cover a simplified allocator and hardware-method model. They do not load a driver, change displays, run RM, or establish hardware timing and firmware behavior.
