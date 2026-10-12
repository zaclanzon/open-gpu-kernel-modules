# SST DSC restoration validation

The [hardware investigation and wake results](hardware-validation.md) describe
the observed failure, the controlled restoration tests, and the FEC limitation.

## Run the regression tests

Run from the repository root with Python 3 and a C++17 compiler:

```sh
python3 tests/sst-dsc-restore/run.py
```

The runner extracts the production DSC mode enum, `GroupImpl::insert`,
`GroupImpl::remove`, and `ConnectorImpl::setDeviceDscState`. Device and list test
doubles exercise lost sink state after reinsertion, restored tracking for an
already enabled sink, single/dual DSC modes, excluded connector types and group
states, ownership conflicts, and a failed configuration followed by retry.

AddressSanitizer and UndefinedBehaviorSanitizer are enabled. Set
`ASAN_OPTIONS=detect_leaks=0` only when the execution environment prevents
LeakSanitizer from running. `CXX` selects a different compiler.

These tests check control flow and tracking lifetime. They do not simulate AUX
transactions, the physical decoder, FEC, HDCP, stream allocation, or kernel
locking. The hardware captures provide separate evidence for the physical wake
behavior; they do not turn the device double into a hardware model.

The six regression scenarios passed under ASan/UBSan. Removing only the DSC
restoration block caused the lost-state scenario to fail as expected.
