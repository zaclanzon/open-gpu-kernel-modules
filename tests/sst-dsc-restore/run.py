#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise production SST DSC restoration with small device test doubles."""
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

TEST_DIR = Path(__file__).resolve().parent
REPO_ROOT = TEST_DIR.parents[1]
DP_SOURCE = REPO_ROOT / "src/common/displayport/src/dp_connectorimpl.cpp"


def source_fragment(path, text, first_line=1):
    """Report diagnostics against the checked-in source."""
    filename = json.dumps(path.relative_to(REPO_ROOT).as_posix())
    return f"#line {first_line} {filename}\n{text}\n"


def read_function(path, signature):
    """Extract one definition, failing clearly if its signature has changed.

    This is a brace scanner, not a C/C++ parser. The selected
    functions must have balanced braces in any comments or string literals too.
    """
    text = path.read_text()
    matches = list(re.finditer(r"^" + re.escape(signature), text, re.MULTILINE))
    if len(matches) != 1:
        raise ValueError(f"{path}: expected one definition of {signature!r}")

    start = matches[0].start()
    opening_brace = text.find("{", start)
    if opening_brace == -1:
        raise ValueError(f"{path}: missing function body for {signature!r}")

    depth = 0
    for position in range(opening_brace, len(text)):
        character = text[position]
        if character == "{":
            depth += 1
        elif character == "}":
            depth -= 1
            if depth == 0:
                first_line = text.count("\n", 0, start) + 1
                return source_fragment(path, text[start:position + 1], first_line)

    raise ValueError(f"{path}: unterminated body for {signature!r}")


def build_and_run(source_path, source, compiler, standard, extra_flags=()):
    source_path.write_text(source)
    binary_path = source_path.with_suffix("")
    command = shlex.split(compiler) + [
        f"-std={standard}",
        "-g",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        *extra_flags,
        str(source_path),
        "-o", str(binary_path),
    ]
    subprocess.run(command, check=True)
    subprocess.run([str(binary_path)], check=True)


def sst_dsc_test_source():
    path = TEST_DIR / "test-sst-dsc.cpp"
    text = path.read_text()
    enum_marker = "/* PRODUCTION_DSC_MODE */"
    function_marker = "/* PRODUCTION_FUNCTIONS */"
    if text.count(enum_marker) != 1 or text.count(function_marker) != 1:
        raise ValueError(f"{path}: expected one marker for each production fragment")

    before_enum, rest = text.split(enum_marker)
    before_functions, after = rest.split(function_marker)
    enum_line = text.count("\n", 0, text.index(enum_marker) + len(enum_marker)) + 1
    after_line = text.count("\n", 0, text.index(function_marker) + len(function_marker)) + 1
    group_source = REPO_ROOT / "src/common/displayport/src/dp_groupimpl.cpp"
    return "\n".join([
        source_fragment(path, before_enum),
        read_function(REPO_ROOT / "src/common/inc/displayport/displayport.h",
                      "enum DSC_MODE") + ";",
        source_fragment(path, before_functions, enum_line),
        read_function(DP_SOURCE, "bool ConnectorImpl::setDeviceDscState("),
        read_function(group_source, "void GroupImpl::insert("),
        read_function(group_source, "void GroupImpl::remove("),
        source_fragment(path, after, after_line),
    ])


def main():
    with tempfile.TemporaryDirectory(prefix="nv-sst-dsc-test-") as directory:
        build_and_run(Path(directory) / "sst-dsc-test.cpp", sst_dsc_test_source(),
                      os.environ.get("CXX", "c++"), "c++17")


if __name__ == "__main__":
    main()
