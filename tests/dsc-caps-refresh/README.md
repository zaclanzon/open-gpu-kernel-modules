# DSC capability refresh validation

On 2026-11-01 a Samsung Odyssey G93SC lost its 5120 × 1440 at 240 Hz mode after
a display wake. The sink's AUX channel failed while it woke (the driver briefly
fell back to 640 × 480), and the DP library cached its DSC support as absent.
DPCD read back healthy afterwards: DSC_SUPPORT 0x01, HBR3 × 4 trained, FEC
capable. A monitor power-button cycle did not help, because a returning device
with an identical EDID is a complete match and keeps its cached caps. Removing
AC power did, because the compositor dropped the monitor and the device was
recreated.

The fix retries a failed DSC_SUPPORT read, and re-reads the DSC and FEC caps of
a returning SST device whose cached caps say it has no DSC. A device that has
DSC keeps its caps.

## Run the regression tests

Run from the repository root with Python 3 and a C++17 compiler:

```sh
python3 tests/dsc-caps-refresh/run.py
```

The runner extracts `DeviceImpl::getDSCSupport`, its two retry defines,
`ConnectorImpl::readDeviceDscCaps` and `ConnectorImpl::refreshReturningDeviceCaps`,
and reuses the extraction helpers of `tests/sst-dsc-restore/run.py`. NVIDIA's
`nvmisc.h` field macros and DPCD headers decode the scripted DSC_SUPPORT bytes.
Test doubles cover retry on failure, the retry budget, devices without reachable
DPCD, successful reads, branch and pass-through caps, GPU, TBT3 and EDID override
paths, a stale sink regaining DSC on return, and returning devices that must keep
their caps. AddressSanitizer and UndefinedBehaviorSanitizer are enabled.
