#!/usr/bin/env python3
"""Check generated CA ownership/physical methods against allocation invariants.

This uses the production functions and hardware register definitions, but a
software method recorder. It does not validate real hardware timing or firmware.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
evo = (root / 'src/nvidia-modeset/src/nvkms-evo4.c').read_text()
start = evo.index('static void EvoTrackPhysicalWindowChangeCA(')
end = evo.index('\nNVEvoHAL nvEvoC9 =', start)
pre = (Path(__file__).with_name('fixtures') / 'ownership-model.h').read_text()
post = (Path(__file__).with_name('fixtures') / 'window-ownership.c').read_text()
with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp) / 'test.c'
    exe = Path(tmp) / 'test'
    c.write_text(pre + evo[start:end] + post)
    subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined',
                    '-I' + str(root / 'src/common/sdk/nvidia/inc'),
                    str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
