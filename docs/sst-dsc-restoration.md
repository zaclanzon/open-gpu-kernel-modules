# Restore DSC after active SST reinsertion

## Observed failure

A Samsung G93SC could remain blank after display standby while its SST head
remained attached with DSC active. Re-detection removed the device from the
active group and its `dscEnabledDevices` tracking list. Reinsertion restored
group membership without reapplying the sink's DSC state.

A diagnostic read immediately before reinsertion succeeded and reported DSC
single mode active with the sink enable bit off. The earlier wake modeset had
already finished. This established a missing-state condition; it did not
establish why the sink lost that bit.

## Change

`GroupImpl::insert` reapplies DSC through the existing `setDeviceDscState`
setter after checking active-group ownership. This restores both sink
decompression and driver tracking for capable DisplayPort sinks on an attached
SST head using single or dual DSC. MST, inactive heads, other connector types,
non-DSC modes, and DSC drop mode keep their existing behavior. Configuration
failure produces a warning and preserves the existing insertion behavior.

## Validation

The production-function tests in `tests/sst-dsc-restore` passed under ASan and
UBSan. Removing only the restoration block made the lost-state test fail.
The device double does not model physical AUX or decoder behavior.

On the diagnostic build, three user-confirmed wakes returned all four displays
with the generic DRM recovery worker disabled. Two wakes exercised the active
SST reinsertion path: a successful read reported DSC disabled, restoration
succeeded, and no additional recovery modeset followed the initial wake
modeset. The third wake completed normally without that reinsertion path.

That diagnostic build also included earlier experimental code. The final build
removes the diagnostic probes, wake tracing, initial-assessment retry, and
generic recovery worker. Its final hardware validation remains pending.
The local hardware build preserves the user's pre-existing overlay changes;
those changes are excluded from this PR.

No root cause for the sink's lost DSC enable bit has been established, and this
capture does not establish behavior across other monitor or converter models.
