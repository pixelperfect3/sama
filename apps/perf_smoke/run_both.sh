#!/usr/bin/env bash
# Run perf_smoke twice (single-threaded, then multi-threaded) and print the
# two budget tables back-to-back so the bgfx::frame() row can be diffed
# visually.  Single-process invocation isn't an option because bgfx::init /
# bgfx::shutdown is one cycle per process in this codebase.
#
# Usage: apps/perf_smoke/run_both.sh [frame_count]
# Frame count defaults to 600 (matches the binary default).
#
# Exit code: max of the two runs (so CI catches a regression in either mode).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="${ROOT}/build/perf_smoke"
FRAMES="${1:-600}"

if [[ ! -x "${BIN}" ]]; then
    echo "perf_smoke binary not built: ${BIN}" >&2
    echo "  cmake --build build --target perf_smoke -j\$(sysctl -n hw.ncpu)" >&2
    exit 2
fi

set +e
"${BIN}" "${FRAMES}" --single
SINGLE_RC=$?
"${BIN}" "${FRAMES}" --multi
MULTI_RC=$?
set -e

if (( SINGLE_RC > MULTI_RC )); then
    exit ${SINGLE_RC}
fi
exit ${MULTI_RC}
