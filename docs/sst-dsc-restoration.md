# Restore DSC when an active SST sink returns

An attached SST group can retain its DSC mode while the returning monitor has
DSC decompression disabled. Group removal clears the device's DSC tracking entry;
reinsertion previously restored membership without reconfiguring sink DSC.

The first diagnostic wake capture showed this sequence on the Samsung G93SC:

| Event | Seconds since boot |
| --- | ---: |
| Initial attach read DSC already enabled | 177.824015 |
| Initial modeset completed | 178.093018 |
| Device entered zombie state | 178.507129 |
| Device exited zombie state | 181.600028 |
| Probe read DSC disabled in an attached DSC_SINGLE group | 181.602003 |
| Recovery modeset enabled DSC | 181.909022 |
| Recovery modeset completed | 181.962041 |

The probe recorded `head=2 dsc-mode=0 tracked=0 read-ok=1 enabled=0`.
`DSC_SINGLE` is enum value 0; `DSC_MODE_NONE` is 3. The AUX query succeeded.
All four displays returned after the existing recovery modesets, as confirmed
by the user. This establishes the state mismatch, but does not show why the
monitor lost the bit or whether it lost other state too.

## Candidate behavior

After validating active-group ownership, insertion uses the existing connector
DSC setter to restore sink enablement and its tracking entry. The call is limited
to an attached SST group with a DSC-capable DisplayPort sink in single or dual
DSC mode. Inactive groups, MST links, HDMI converters, and drop/non-DSC modes do
not use this path. The existing setter avoids rewriting an already enabled bit
and adds the tracking entry only after successful configuration.

`DP-SST-RETURN>` records the state before restoration. `DP-SST-RESTORE>` reports
the setter result. A failed setter does not add a DSC tracking entry; membership
insertion continues, preserving the existing group API. No full modeset is
requested by this new code.

## Validation and remaining hardware test

The production insertion/removal methods and connector setter are exercised by
named tests, including a simulated lost enable bit, an already enabled sink,
configuration failure and retry, and the excluded device/group states. The
previous insertion code fails the lost-state scenario. Tests do not emulate
the monitor decoder, AUX timing, or real kernel locking.

The local hardware bundle retains the working V3 baseline and diagnostic probe,
with this restoration as its only additional production-source change. The
fork branch is based on the isolated recovery/diagnostic branches instead.

To test whether DSC restoration alone is sufficient, boot the candidate with
the existing recovery behavior enabled, verify the loaded build IDs, then
disable the generic recovery worker for a controlled wake cycle. Capture the
probe, restoration result, and all modeset events, and record the Samsung and
HDMI outcomes separately. A wake with an extra recovery modeset cannot establish
that the DSC-only change was sufficient. Hardware validation is pending.
