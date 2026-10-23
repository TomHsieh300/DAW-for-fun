#!/usr/bin/env bash
# Configure, build and test the DawLab course (Linux / macOS; the counterpart of build.ps1).
#
# Examples:
#   ./scripts/build.sh                                  # Build all pure C++ chapters and run every test
#   ./scripts/build.sh --only 02-filter                 # Build only chapter 02
#   ./scripts/build.sh --juce                           # Also build the JUCE programs of chapters 13 and 14
#   ./scripts/build.sh --only 13-vst-hosting,14-daw-8track --juce   # Separate multiple chapters with commas
#   ./scripts/build.sh --config Release --no-test
#
# Uses a single-config generator (Unix Makefiles, or Ninja if installed), so executables land in
# <build>/bin/ with no Debug/ or Release/ subfolder.
set -euo pipefail

only=""
build_dir="build"
config="Debug"
juce="OFF"
run_tests=1
test_label=""

usage() {
    sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'
    cat <<'EOF'
Options:
  --only <a,b,...>     build only these chapters (folder names, comma-separated)
  --build-dir <dir>    build folder (default: build)
  --config <cfg>       Debug | Release | RelWithDebInfo (default: Debug)
  --juce               also build chapters 13 and 14 (JUCE)
  --no-test            build only, do not run ctest
  --test-label <lbl>   run only tests with this label (example, solution, test)
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --only)       only="$2"; shift 2 ;;
        --build-dir)  build_dir="$2"; shift 2 ;;
        --config)     config="$2"; shift 2 ;;
        --juce)       juce="ON"; shift ;;
        --no-test)    run_tests=0; shift ;;
        --test-label) test_label="$2"; shift 2 ;;
        -h|--help)    usage; exit 0 ;;
        *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

case "$config" in
    Debug|Release|RelWithDebInfo) ;;
    *) echo "--config must be Debug, Release or RelWithDebInfo" >&2; exit 2 ;;
esac

command -v cmake >/dev/null || { echo "CMake not found. Install it (Ubuntu: sudo apt install build-essential cmake)." >&2; exit 1; }

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[[ "$build_dir" = /* ]] || build_dir="$root/$build_dir"

configure_args=(-S "$root" -B "$build_dir" "-DCMAKE_BUILD_TYPE=$config"
                "-DDAWLAB_ONLY=${only//,/;}" "-DDAWLAB_WITH_JUCE=$juce")
# Pick Ninja only for a fresh build folder; an existing one keeps its generator
if command -v ninja >/dev/null && [[ ! -f "$build_dir/CMakeCache.txt" ]]; then
    configure_args+=(-G Ninja)
fi

echo "==> configure ($build_dir)"
cmake "${configure_args[@]}"

echo "==> build ($config)"
cmake --build "$build_dir" --parallel

if [[ $run_tests -eq 1 ]]; then
    echo "==> test"
    test_args=(--test-dir "$build_dir" --output-on-failure)
    [[ -n "$test_label" ]] && test_args+=(-L "$test_label")
    ctest "${test_args[@]}"
fi
