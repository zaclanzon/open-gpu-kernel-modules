#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise production DSC capability reads with small device test doubles."""
import importlib.util
import os
from pathlib import Path
import tempfile

TEST_DIR = Path(__file__).resolve().parent
REPO_ROOT = TEST_DIR.parents[1]
DP_SOURCE = REPO_ROOT / "src/common/displayport/src"

# Reuse the extraction and build helpers of the SST DSC restoration suite.
_spec = importlib.util.spec_from_file_location(
    "sst_dsc_restore", TEST_DIR.parent / "sst-dsc-restore/run.py")
_restore = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_restore)


def dsc_caps_test_source():
    path = TEST_DIR / "test-dsc-caps.cpp"
    text = path.read_text()
    marker = "/* PRODUCTION_FUNCTIONS */"
    if text.count(marker) != 1:
        raise ValueError(f"{path}: expected one production marker")

    before, after = text.split(marker)
    after_line = text.count("\n", 0, text.index(marker) + len(marker)) + 1
    return "\n".join([
        _restore.source_fragment(path, before),
        _restore.read_function(DP_SOURCE / "dp_deviceimpl.cpp", "NvBool DeviceImpl::getDSCSupport("),
        _restore.read_function(DP_SOURCE / "dp_connectorimpl.cpp", "void ConnectorImpl::readDeviceDscCaps("),
        _restore.read_function(DP_SOURCE / "dp_connectorimpl.cpp",
                               "void ConnectorImpl::refreshReturningDeviceCaps("),
        _restore.source_fragment(path, after, after_line),
    ])


def main():
    includes = [f"-I{REPO_ROOT / 'src/common/sdk/nvidia/inc'}", f"-I{REPO_ROOT / 'src/common/inc'}"]
    with tempfile.TemporaryDirectory(prefix="nv-dsc-caps-test-") as directory:
        _restore.build_and_run(Path(directory) / "dsc-caps-test.cpp", dsc_caps_test_source(),
                               os.environ.get("CXX", "c++"), "c++17", includes)


if __name__ == "__main__":
    main()
