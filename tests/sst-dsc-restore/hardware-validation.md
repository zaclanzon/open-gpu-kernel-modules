# Hardware investigation and wake validation

## Setup and reproduction

The observed failure affected a Samsung Odyssey G93SC connected over DisplayPort
SST to an RTX 5090, using 5120x1440 at 240 Hz with single DSC. The desktop ran
Ubuntu with GNOME Wayland, NVIDIA 610.57.04, and kernel 7.0.0-31-generic, with
four displays connected. Validation described here was completed on 2026-09-10.

The reproduction used the usual display-off shortcut, waited for the Samsung
to enter power-saving mode, and then woke the displays normally. The Samsung
could remain blank even while its connector was reported connected and enabled
and the driver retained an attached head with DSC active.

The local hardware build also retained an existing Blackwell overlay change
tracked in [upstream PR #1338](https://github.com/NVIDIA/open-gpu-kernel-modules/pull/1338).
That change was held constant during this investigation and is excluded from
the DSC fix.

## Observed state and restoration

During re-detection, `notifyZombieStateChange()` removed the returning device
from its active group. `GroupImpl::remove()` also removed its entry from
`dscEnabledDevices` to avoid retaining a dangling pointer. The original
`GroupImpl::insert()` restored group membership without reapplying DSC.

A successful DPCD read immediately before reinsertion reported the sink's DSC
enable bit off while the attached group still used single DSC. The initial wake
modeset had already completed. The restoration called `setDeviceDscState()` to
enable sink decompression and restore the driver's tracking entry.

The logs recorded a successful enable operation, followed by user-confirmed
physical recovery. They did not include a separate readback of the DSC enable
bit after the write. Why the bit became clear remains unproven; a particular
sink power transition has not been established as its cause.

There is an existing state-restoration precedent in this layer: the comment in
[`ConnectorEventSink::notifyZombieStateChange()`](https://github.com/NVIDIA/open-gpu-kernel-modules/blob/e4a5faa2567f28c8eabe0ebb6422b6d0abcf37eb/src/nvidia-modeset/src/dp/nvdp-connector-event-sink.cpp#L477-L481)
explains why the DP library must initiate VRR re-enablement for a returning
device before link training. This supports restoring a sink feature in the DP
library; it is not evidence that DSC and VRR have identical sequencing needs.

## Controlled diagnostic wakes

The diagnostic candidate retained earlier experimental code, but its generic
DRM recovery worker was disabled before these tests. Three consecutive
user-confirmed wakes returned all four displays. Two exercised SST reinsertion;
the third completed through the normal wake path.

| Wake | Initial modeset completed (s) | DSC read off (s) | Enable operation succeeded (s) | Physical outcome |
| --- | ---: | ---: | ---: | --- |
| 1 | 264.987017 | 269.815041 | 269.816017 | All four displays returned |
| 2 | 1006.025009 | 1010.193014 | 1010.193035 | All four displays returned |
| 3 | 1033.994016 | Not exercised | Not exercised | All four displays returned |

Times are monotonic seconds within the diagnostic boot. In wakes 1 and 2, no
additional modeset followed the initial wake modeset. Recovery requests were
still emitted, but the disabled worker did not queue or reapply a recovery
modeset. These two captures support the DSC restoration as sufficient for the
observed reinsertion failure on this setup.

## FEC scope

Both reinsertion captures requested skipping redundant link training before
DSC restoration. That request did not bypass the entire `train()` function:
the existing path could still call `configureFec(true)` under its FEC and link
conditions. The DSC patch did not change that FEC handling.

The sink's FEC configuration register was not read in these captures. Its
state, and whether it needed restoration, therefore cannot be established from
these logs. The successful wakes do not establish FEC behavior across other
sinks, converters, or MST topologies.

## Final cleaned-build check

After the diagnostic tests, a separate final build removed the diagnostic
probes, wake tracing, initial-assessment retry, and generic recovery worker.
Its loaded identities were verified, and one further user-confirmed wake check
returned all four displays. This final check is distinct from the three
diagnostic wakes above.

- nvidia-modeset build ID: `07480f2fc6071abc1a448bce4d3588ccc57c2c9f`
- nvidia-drm build ID: `5ff0bd005afc162afbe8dace3bf020c67ffe1c10`

The final check used normal `debug=0` logging, so it did not record which
reinsertion branch was taken. `DP_PRINTF` suppresses warnings at that setting;
the absence of a restoration warning is not independent evidence of success.
The physical result and loaded build identities establish this check's outcome.

The latest source changes only the failure-log wording and line formatting
relative to that binary; the restoration control flow is unchanged. The paired
module build and imported symbol checks passed before installation, as did the
installer's failure/rollback and file-permission checks.

## Newer upstream source

The final patch also applied cleanly to upstream 615.71.09 at commit
`61dcc93722ecb418bb5f2e00923f05b4b8051dd1`. The same regression harness passed
against the selected production functions from that source. The DSC enum,
group removal, connector DSC setter, and device DSC getter/setter matched the
610.57.04 implementations. This was a source-level regression check, not a full
615.71.09 driver build or hardware test.
