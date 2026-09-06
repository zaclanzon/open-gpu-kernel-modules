#!/usr/bin/env python3
"""Test production physical-window tracking and modeset/flip ordering."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
evo = (root / 'src/nvidia-modeset/src/nvkms-evo4.c').read_text()
tracking_start = evo.index('static void EvoTrackPhysicalWindowChangeCA(')
tracking_end = evo.index('static void EvoSetWindowOwnerCA(', tracking_start)
modeset = (root / 'src/nvidia-modeset/src/nvkms-modeset.c').read_text()
update_start = modeset.index('static void\nKickoffProposedModeSetHwState(')
update_end = modeset.index('static void AllocatePostModesetDispBandwidth(', update_start)
fixtures = Path(__file__).with_name('fixtures')
preamble = (fixtures / 'sequencing-model.h').read_text()
scenarios = (fixtures / 'phywin-sequencing.c').read_text()

with tempfile.TemporaryDirectory() as directory:
    source = Path(directory) / 'test.c'
    binary = Path(directory) / 'test'
    source.write_text(preamble + evo[tracking_start:tracking_end] +
                      modeset[update_start:update_end] + scenarios)
    subprocess.run([
        'cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
        '-Wno-unused-parameter', '-Wno-unused-variable',
        '-fsanitize=address,undefined', str(source), '-o', str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
