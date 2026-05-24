#!/usr/bin/env bash
# Run perf_smoke twice (single-threaded, then multi-threaded) and print the
# two budget tables back-to-back so the bgfx::frame() row can be diffed
# visually.  Single-process invocation isn't an option because bgfx::init /
# bgfx::shutdown is one cycle per process in this codebase.
#
# Usage: apps/perf_smoke/run_both.sh [frame_count]
# Frame count defaults to 600 (matches the binary default).
#
# Exit code:
#   0  — both runs PASS their per-system budgets AND multi-threaded
#        bgfx::frame() mean is meaningfully smaller than single-threaded
#        (the actual point of the flag).
#   1  — at least one run failed its budgets, OR multi-threaded didn't
#        improve bgfx::frame() mean over single-threaded by more than the
#        tolerance below (which would mean the threading-mode flag is
#        silently broken — e.g. someone unintentionally re-enabled the
#        single-threaded path in Renderer::init).
#   2  — perf_smoke binary missing / not built.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="${ROOT}/build/perf_smoke"
FRAMES="${1:-600}"

# Minimum bgfx::frame() mean improvement multi vs single, as a ratio of
# single-threaded mean.  On desktop M-series the observed gap is ~3x even
# without GPU saturation; on Pixel 9 it's ~200x.  A 30% improvement is a
# very conservative floor that still catches "flag silently does nothing"
# while tolerating noise on fast / lightly-loaded machines.
MIN_IMPROVEMENT_RATIO="0.30"

if [[ ! -x "${BIN}" ]]; then
    echo "perf_smoke binary not built: ${BIN}" >&2
    echo "  cmake --build build --target perf_smoke -j\$(sysctl -n hw.ncpu)" >&2
    exit 2
fi

# Capture each run's stdout so we can both display it AND grep the
# bgfx::frame() row for the regression check.  `tee /dev/tty` does both
# in one pass without buffering surprises.
SINGLE_LOG="$(mktemp -t perf_smoke_single.XXXXXX)"
MULTI_LOG="$(mktemp -t perf_smoke_multi.XXXXXX)"
trap 'rm -f "${SINGLE_LOG}" "${MULTI_LOG}"' EXIT

set +e
"${BIN}" "${FRAMES}" --single | tee "${SINGLE_LOG}"
SINGLE_RC=${PIPESTATUS[0]}
"${BIN}" "${FRAMES}" --multi  | tee "${MULTI_LOG}"
MULTI_RC=${PIPESTATUS[0]}
set -e

# Pull the bgfx::frame() mean from each run.  The row format is:
#   "  bgfx::frame()          |   X.XXX |   Y.YYY |   Z.ZZZ | (info)"
# awk strips the leading "|" columns and prints the mean (field 3 after
# the system name, which is itself the first non-pipe block).
extract_bgfx_mean() {
    awk -F'|' '/bgfx::frame\(\)/ { gsub(/ /, "", $2); print $2; exit }' "$1"
}
SINGLE_BGFX="$(extract_bgfx_mean "${SINGLE_LOG}")"
MULTI_BGFX="$(extract_bgfx_mean "${MULTI_LOG}")"

if [[ -z "${SINGLE_BGFX}" || -z "${MULTI_BGFX}" ]]; then
    echo >&2
    echo "run_both.sh: could not parse bgfx::frame() mean from one or both runs" >&2
    echo "  single='${SINGLE_BGFX}'  multi='${MULTI_BGFX}'" >&2
    echo "  (regression-check skipped; reporting per-run budget exit codes only)" >&2
else
    # Compute (single - multi) / single and require it >= MIN_IMPROVEMENT_RATIO.
    # awk handles the float math portably (avoids shell-only integer arithmetic
    # and the bc dependency).
    THREAD_REGRESSION="$(awk -v s="${SINGLE_BGFX}" -v m="${MULTI_BGFX}" \
        -v t="${MIN_IMPROVEMENT_RATIO}" \
        'BEGIN { if (s <= 0) { print "skip"; exit }
                 r = (s - m) / s;
                 printf "ratio=%.3f single=%.3f multi=%.3f threshold=%.3f\n", r, s, m, t;
                 if (r < t) print "FAIL"; else print "OK" }')"
    echo
    echo "=== threading-mode regression check ==="
    echo "  ${THREAD_REGRESSION}"
    if [[ "${THREAD_REGRESSION}" == *"FAIL"* ]]; then
        echo "  multi-threaded bgfx::frame() mean did NOT improve over single-threaded" >&2
        echo "  by at least ${MIN_IMPROVEMENT_RATIO} — flag may be silently broken." >&2
        if (( SINGLE_RC == 0 )); then SINGLE_RC=1; fi
    fi
fi

if (( SINGLE_RC > MULTI_RC )); then
    exit ${SINGLE_RC}
fi
exit ${MULTI_RC}
