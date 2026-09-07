#!/usr/bin/env python3
"""Exercise the production CA allocator with a small standalone hardware model."""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] /
          'src/nvidia-modeset/src/nvkms-evo4.c').read_text()
start = source.index('static NvBool RequiredScalerTiles(')
end = source.index('static void\nEvoIsModePossibleCA(', start)
modeset = (Path(__file__).resolve().parents[1] /
           'src/nvidia-modeset/src/nvkms-modeset.c').read_text()
move_start = modeset.index('static NvBool IsCurrentMultiTileConfigOneApiHeadIncompatible(')
move_end = modeset.index('\n/*!', move_start)
preamble = (Path(__file__).with_name('fixtures') / 'allocation-model.h').read_text()
tests = (Path(__file__).with_name('fixtures') / 'phywin-allocation.c').read_text()
with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp) / 'test.c'
    exe = Path(tmp) / 'test'
    c.write_text(preamble + source[start:end] + modeset[move_start:move_end] + tests)
    subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=undefined,address', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
