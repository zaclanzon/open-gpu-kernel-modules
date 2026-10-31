#!/usr/bin/env python3
"""Compare hardware-head assignment with the pinned upstream implementation.

Runs the generic head assignment from nvkms-modeset.c against 615.71.09 over
every mix of HDMI, DP, DP SST DSC, DP MST DSC and 2Heads1OR dpys on two to
four heads, with uneven layer counts and heads already in use. Assignments
must succeed exactly when stock succeeds, stay valid, and match stock unless
stock put a DP SST DSC dpy on Blackwell hardware head 0.
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


def assignment(text, first_helper):
    return (between(text, first_helper, 'static\nNvBool AssignProposedHwHeadsForDPSerializer(') +
            between(text, 'static NvU32 GetFree2Heads1ORHeadsMask(',
                    'static void ClearHwHeadsMaskOneApiHead('))


current = assignment((root / source_path).read_text(), 'static NvBool ApiHeadFitsHwHead(')
reference = assignment(subprocess.check_output(
    ['git', 'show', reference_revision + ':' + source_path], cwd=root, text=True),
    'static NvBool HeadIsFree(')
for name in re.findall(r'static\s+(?:NvBool|NvU32)\s+(\w+)\s*\(', reference):
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
