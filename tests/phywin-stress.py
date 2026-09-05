"""Stress production allocation with an independent capacity/uniqueness oracle.

The model covers four heads, eight tiles, two layers per head, mixed scaler
capabilities, fresh/reused assignments, and 8/32 physical-window limits.
It tests software allocation only, not RM acceptance or hardware execution.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
preamble = (Path(__file__).with_name('fixtures') / 'allocation-model.h').read_text()
# Exercise the API's eight-layer array limit as well as two-layer desktops.
preamble = preamble.replace('[2]', '[8]')
source = (root / 'src/nvidia-modeset/src/nvkms-evo4.c').read_text()
start = source.index('static NvBool RequiredScalerTiles(')
end = source.index('static void\nEvoIsModePossibleCA(', start)
scenarios = (Path(__file__).with_name('fixtures') / 'phywin-stress.c').read_text()
with tempfile.TemporaryDirectory() as directory:
    test = Path(directory) / 'test.c'
    binary = Path(directory) / 'test'
    test.write_text(preamble + source[start:end] + scenarios)
    subprocess.run([
        'cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
        '-fsanitize=address,undefined', str(test), '-o', str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
