#!/usr/bin/env python3
"""Compile the production worker against fault-injectable DRM/workqueue doubles.

Only includes are replaced; the worker and policy bodies come from the tree.
This checks control flow and state lifetime, not real kernel locks or hardware.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

here = Path(__file__).resolve().parent
root = here.parents[1]
worker = root / 'kernel-open/nvidia-drm/nvidia-drm-link-recovery.c'
policy = root / 'kernel-open/nvidia-drm/nvidia-drm-link-recovery-policy.h'
def function(path, name):
    text = path.read_text()
    start = text.index(name)
    brace = text.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


with tempfile.TemporaryDirectory(prefix='nv-link-recovery-test-') as tmp:
    test = Path(tmp) / 'worker-test.c'
    test.write_text(
        (here / 'mock-drm.h').read_text()
        + '\n' + re.sub(r'^#include[^\n]*$', '', policy.read_text(), flags=re.M)
        + '\n' + (here / 'mock-drm.c').read_text()
        + '\n' + re.sub(r'^#include[^\n]*$', '', worker.read_text(), flags=re.M)
        + '\n' + function(root / 'kernel-open/nvidia-drm/nvidia-drm-modeset.c',
                            'static void nv_drm_recover_failed_modeset(')
        + '\n' + (here / 'test-worker.c').read_text())
    binary = Path(tmp) / 'worker-test'
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-g',
                    '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    str(test), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

    dp = Path(tmp) / 'dp-test.cpp'
    functions = [
        function(root / 'src/common/displayport/src/dp_connectorimpl.cpp',
                 'NvU32 ConnectorImpl::dpPreModeset('),
        function(root / 'src/common/displayport/src/dp_connectorimpl.cpp',
                 'void ConnectorImpl::dpPostModeset('),
        function(root / 'src/nvidia-modeset/src/nvkms.c',
                 'void nvSendDpyLinkRecoveryEventEvo('),
    ]
    dp.write_text((here / 'test-dp.cpp').read_text().replace(
        '/* PRODUCTION_FUNCTIONS */', '\n'.join(functions)))
    binary = Path(tmp) / 'dp-test'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-g',
                    '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    str(dp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
