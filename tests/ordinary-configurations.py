#!/usr/bin/env python3
"""Compare ordinary CA allocations with a pinned upstream implementation.

All exposed layers are usable in these cases. Inputs stay inside the physical
window budget. Known upstream incomplete-retry results are counted separately;
only complete successful reference allocations are compared for exact equality.
Hardware methods and timings are not simulated here.
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source_path = 'src/nvidia-modeset/src/nvkms-evo4.c'
reference_revision = 'e4a5faa2567f28c8eabe0ebb6422b6d0abcf37eb'
preamble = (Path(__file__).with_name('fixtures') / 'allocation-model.h').read_text()


def allocator(text):
    start = text.index('static NvBool RequiredScalerTiles(')
    end = text.index('static void\nEvoIsModePossibleCA(', start)
    return text[start:end]


current = allocator((root / source_path).read_text())
reference = allocator(subprocess.check_output(
    ['git', 'show', reference_revision + ':' + source_path], cwd=root, text=True))
function_names = re.findall(
    r'(?:static\s+)(?:NvBool|NvU32|void|NVEvoHwTileType)\s+(\w+)\s*\(', reference)
for name in function_names:
    reference = re.sub(r'\b' + re.escape(name) + r'\b', 'Reference' + name, reference)

scenarios = (Path(__file__).with_name('fixtures') / 'ordinary-configurations.c').read_text()
with tempfile.TemporaryDirectory() as directory:
    test = Path(directory) / 'comparison.c'
    binary = Path(directory) / 'comparison'
    test.write_text(preamble + reference + current + scenarios)
    subprocess.run([
        'cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
        '-fsanitize=address,undefined', str(test), '-o', str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
