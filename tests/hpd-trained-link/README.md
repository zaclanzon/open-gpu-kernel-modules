# Still-trained link on HPD validation

On 2026-11-13 a Samsung Odyssey G93SC raised a long HPD pulse while its head
was attached. The hotplug work ran `assessLink()`, which put the active head in
flush mode and retrained it. On GB20x the phase 1 flush entry and the link
training each took about 30 s in GSP, and `nvidia-modeset/kthread_q` held the
nvkms lock and the RM API lock for 56 s. The compositor and every other RM
client waited on them.

The fix reads the sink's power state and link status when an SST sink returns
with a head attached. A link that is still trained keeps its last assessment.
A sink that lost its link is assessed as before.

## Run the regression tests

Run from the repository root with Python 3 and a C++17 compiler:

```sh
python3 tests/hpd-trained-link/run.py
```

The runner extracts `ConnectorImpl::getActiveGroupForSST`,
`ConnectorImpl::isLinkActive`, `ConnectorImpl::isLinkLost` and
`ConnectorImpl::isActiveSstLinkStillTrained`, and reuses the extraction helpers
of `tests/sst-dsc-restore/run.py`. Test doubles cover a trained link on an
attached head, each lost link status bit with a stale cache that still reads
healthy, D3 and offline sinks, a lane count of 0 in RM, and the head, group,
MST, transition and quiesced states that must still be assessed.
AddressSanitizer and UndefinedBehaviorSanitizer are enabled.

These tests do not load a driver or run GSP. Whether the G9 keeps its link
across the pulse is a hardware question; the `z-trace: DP long pulse` and
`z-trace: DP rm control` lines answer it per event.
