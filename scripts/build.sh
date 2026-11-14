#!/usr/bin/env bash
# Configure, build and test the DawLab course (Linux / macOS; the counterpart of build.ps1).
#
# Examples:
#   ./scripts/build.sh                                  # Build all pure C++ chapters and run every test
#   ./scripts/build.sh --only 02-filter                 # Build only chapter 02
#   ./scripts/build.sh --juce                           # Also build the JUCE programs (chapters 13 and up)
#   ./scripts/build.sh --only 13-vst-hosting,14-daw-8track --juce   # Separate multiple chapters with commas
#   ./scripts/build.sh --only juce --juce               # Every chapter with JUCE programs ("engine": with an engine)
#   ./scripts/build.sh --config Release --no-test
#   ./scripts/build.sh --only 15-daw-growing --sanitize thread --build-dir build-tsan
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
sanitize=""

usage() {
    sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'
    cat <<'EOF'
Options:
  (every option with a <value> needs one)
  --only <a,b,...>     build only these chapters (folder names, comma-separated), or the groups
                       juce (chapters with JUCE programs) and engine (chapters with an engine/);
                       any other name is an error; '' means every chapter. A test run that finds no test fails.
  --build-dir <dir>    build folder (default: build)
  --config <cfg>       Debug | Release | RelWithDebInfo (default: Debug)
  --juce               also build the JUCE programs (chapters 13 and up)
  --no-test            build only, do not run ctest
  --test-label <lbl>   run only tests with this label (example, solution, test)
  --sanitize <kind>    build with -fsanitize=<kind>: thread, address, undefined or address,undefined
                       (use its own --build-dir: every file is compiled differently)
EOF
}

# Every option with a <value> needs one, and the next option is not a value: "--only --juce" is a mistake, not a
# chapter called --juce. An empty value is the default of --only, --test-label and --sanitize (--only '' builds every
# chapter, like build.ps1 -Only '').
need_value() {
    if [[ $# -lt 2 || $2 == -* ]]; then
        echo "$1 needs a value" >&2
        usage >&2
        exit 2
    fi
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --only)       need_value "$@"; only="$2"; shift 2 ;;
        --build-dir)  need_value "$@"; build_dir="$2"; shift 2 ;;
        --config)     need_value "$@"; config="$2"; shift 2 ;;
        --juce)       juce="ON"; shift ;;
        --no-test)    run_tests=0; shift ;;
        --test-label) need_value "$@"; test_label="$2"; shift 2 ;;
        --sanitize)   need_value "$@"; sanitize="$2"; shift 2 ;;
        -h|--help)    usage; exit 0 ;;
        *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

[[ -n "$build_dir" ]] || { echo "--build-dir needs a value" >&2; usage >&2; exit 2; }

case "$config" in
    Debug|Release|RelWithDebInfo) ;;
    *) echo "--config must be Debug, Release or RelWithDebInfo" >&2; exit 2 ;;
esac

command -v cmake >/dev/null || { echo "CMake not found. Install it (Ubuntu: sudo apt install build-essential cmake)." >&2; exit 1; }

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[[ "$build_dir" = /* ]] || build_dir="$root/$build_dir"

configure_args=(-S "$root" -B "$build_dir" "-DCMAKE_BUILD_TYPE=$config"
                "-DDAWLAB_ONLY=${only//,/;}" "-DDAWLAB_WITH_JUCE=$juce" "-DDAWLAB_SANITIZE=$sanitize")
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
    test_args=(--test-dir "$build_dir" --output-on-failure --no-tests=error)
    [[ -n "$test_label" ]] && test_args+=(-L "$test_label")
    ctest "${test_args[@]}"
fi
