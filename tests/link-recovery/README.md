# Link recovery regression tests

Run from the repository root:

```sh
python3 tests/link-recovery/run.py
```

The runner compiles the production recovery worker, retry policy, failed-modeset routing helper, DP pre/post modeset methods, and NVKMS event helper against small fault-injectable doubles. It uses AddressSanitizer and UndefinedBehaviorSanitizer and checks that every allocated mock atomic state is released.

Coverage includes event coalescing and generation wrap; events arriving during commit; independent connectors; inactive and unplugged outputs; allocation and state-acquisition failures; errors before and after state swap; lock backoff with a concurrent mode change or unplug; retry limits under a continuous event storm; pause during commit; and routing failed modesets without retrying ordinary flips or disabled outputs. The DP test exhausts all 4096 combinations of selected heads, attaching heads, and failed heads and verifies balanced pre/post notifications.

The mixed-display regression completes one connector's recovery while another connector has stale disconnected status, then verifies that the second request remains pending and uses the current mode after detection catches up. Detection-wait tests cover expiry, disable, unassign, pause, and an event storm that must not extend the two-second deadline. NVKMS event tests cover invalid, empty, and changing head assignments; those transient states must not drop a recovery event.

In a tracing sandbox where LeakSanitizer cannot inspect the process, use `ASAN_OPTIONS=detect_leaks=0 python3 tests/link-recovery/run.py`. Address and undefined-behavior checks remain enabled; the mock allocation/release assertions remain active.

These are control-flow tests. They do not execute real kernel locking, hardware, MST payload allocation, DSC, HDMI FRL training, or HDR/LUT programming. The full kernel build checks API integration. Hardware and kernel lockdep validation remain necessary before treating the change as production validated.

The SST DSC tests compile the production group insertion/removal methods and
connector DSC state setter against a simulated sink. They reproduce a device
leaving an attached DSC group, losing its sink enable bit, and returning. They
check restoration before the group update, tracking-list repair, single/dual
DSC modes, exclusion of inactive/MST/non-DP/incapable devices, conflicting group
ownership, and configuration failure followed by a retry. Sink configuration
is simulated; these tests do not establish that DSC restoration alone recovers
the physical display.

## Test organization

- `test-worker.c` contains named recovery scenarios with separate setup, actions, and assertions. Failure locations use an enum rather than numbered cases.
- `mock-drm.h` provides the kernel types needed by the worker. `mock-drm.c` keeps device state, injected failures, and observed calls in one fixture. One-shot errors and persistent post-swap failures are distinguished explicitly.
- `test-dp.cpp` checks every selected/attaching/failed head combination and verifies that transient head assignments preserve recovery events. Its layout follows the surrounding DisplayPort C++ code.
- `test-sst-dsc.cpp` exercises the SST return path using production group methods and the connector DSC state setter. The production DSC enum is included in the generated test.
- `run.py` assembles and compiles the production fragments. Compiler and assertion diagnostics point back to the original source files and lines. Function extraction requires one matching signature and uses a brace scanner, so it is not a general C/C++ parser.

The C tests follow the nearby nvidia-drm style: four-space indentation, function braces on their own lines, and one statement per line. Each worker scenario prints its name before running, so an assertion failure identifies the scenario as well as the source location.
