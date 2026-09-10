# SST return diagnostics

This probe reads the sink's DSC enable state when a device is inserted into an already attached DSC group on an SST link. It runs before insertion changes group membership and before the deferred DRM recovery modeset. It adds one AUX read and a diagnostic message; it does not write DSC state or request a modeset.

## Evidence so far

The three successful Samsung wake captures had this order:

| Cycle | Initial modeset completed | Zombie entered | Zombie exited | Recovery request |
| --- | ---: | ---: | ---: | ---: |
| 1 | 137.699017 s | 138.115061 s | 144.825174 s | 144.825183 s |
| 2 | 271.705019 s | 272.141059 s | 275.440022 s | 275.440036 s |
| 3 | 310.245009 s | 310.729006 s | 315.040034 s | 315.040052 s |

The request immediately follows the zombie-exit callback in each capture. No Samsung attach-failure recovery request appeared during the preceding initial modesets. This supports investigating restoration after re-detection. It does not establish the cause of the sink's later loss of availability, a physical HPD voltage transition, or which receiver state was lost.

The accessible source contains the relevant path:

- `ConnectorEventSink::notifyZombieStateChange()` removes a zombie device from its group and reinserts it when it returns.
- `GroupImpl::insert()` restores group membership and calls `update()`. The latter handles MST payload allocation and returns without that work on SST links, which have no message manager.
- `ConnectorImpl2x::notifyAttachBegin()` configures sink DSC during attach. Group insertion does not call that DSC configuration path.

Link assessment also runs during re-detection. The absence of a DSC call in group insertion does not prove that all stream restoration is missing, or that DSC is the cause of the blank screen.

## Reading the probe

Look for `DP-SST-RETURN>` before the recovery modeset in the kernel log. The fields are:

| Field | Meaning |
| --- | --- |
| `head` | Physical head attached to the group |
| `dsc-mode` | Group's current DSC mode enum |
| `tracked` | Whether the device is in the connector's DSC-enabled device list |
| `read-ok` | Whether the fresh DSC enable query succeeded |
| `enabled` | Sink's DSC decompression enable bit; meaningful only if `read-ok=1` |

`read-ok=1 enabled=0` would show a mismatch between the active DSC group and the sink at re-insertion. That would justify testing restoration of DSC state at this point with the generic recovery worker disabled for that separate experiment.

`read-ok=1 enabled=1` would leave decoder state, PPS, MSA, FEC, and source programming as possibilities. An enabled bit does not verify a working decoder. A failed query must be treated as unknown, not as DSC being disabled.

The probe requires an attached DSC group on an SST link. No message is expected for an inactive group, a non-DSC mode, or an MST link. The additional AUX read can affect timing, so compare the event order with the earlier captures.

## Hardware build baseline

The diagnostic branch adds this probe to the isolated recovery branch. The local hardware test bundle applies the identical probe to the V3 production source used for the successful wake captures. That controlled baseline retains the overlay fix, initial-assessment retry, and tracing from V3, with DRM recovery enabled. It is not a hardware test of the isolated recovery branch.

This remains a diagnostic experiment. No root-cause fix is established by the source analysis alone.
