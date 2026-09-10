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
