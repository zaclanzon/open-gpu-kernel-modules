#!/usr/bin/env python3
"""Compare hardware-head assignment with the pinned upstream implementation.

Runs AssignProposedHwHeadsOneDisp() from nvkms-modeset.c against 615.71.09
over mixes of HDMI, DP, DP SST DSC, DP MST DSC, DP serializer and 2Heads1OR
dpys on two to four heads, where each api-head is new, changed on a kept
hardware head, or unchanged. Assignments must succeed exactly when stock
succeeds, stay valid, never leave one api-head's hardware-head state on
another's head, and match stock unless stock left a changed DP SST DSC dpy
on Blackwell hardware head 0.
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source_path = 'src/nvidia-modeset/src/nvkms-modeset.c'
reference_revision = '61dcc93722ecb418bb5f2e00923f05b4b8051dd1'  # NVIDIA 615.71.09
fixtures = Path(__file__).with_name('fixtures')
preamble = (fixtures / 'hw-head-model.h').read_text()


def between(text, start, end):
    first = text.index(start)
    return text[first:text.index(end, first)]


def assignment(text):
    return between(text, 'static\nNvBool AssignProposedHwHeadsForDsiConnector(',
                   '/*!\n * Assign the NVProposedModeSetHwState structure.')


current = assignment((root / source_path).read_text())
reference = assignment(subprocess.check_output(
    ['git', 'show', reference_revision + ':' + source_path], cwd=root, text=True))
for name in re.findall(r'static\s+(?:NvBool|NvU32|void)\s+(\w+)\s*\(', reference):
    reference = re.sub(r'\b' + re.escape(name) + r'\b', 'Reference' + name, reference)

scenarios = (fixtures / 'hw-head-assignment.c').read_text()
with tempfile.TemporaryDirectory() as directory:
    test = Path(directory) / 'hw-head-assignment.c'
    binary = Path(directory) / 'hw-head-assignment'
    test.write_text(preamble + reference + current + scenarios)
    subprocess.run([
        'cc', '-std=c99', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
        '-Wno-unused-parameter',
        '-fsanitize=address,undefined', str(test), '-o', str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
