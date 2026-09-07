#!/usr/bin/env bash
# Run the software models against production sources in this checkout.
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."

export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"

for suite in phywin-allocation phywin-sequencing window-ownership phywin-stress ordinary-configurations; do
    printf '\nRunning %s\n' "$suite"
    python3 "tests/$suite.py"
done
