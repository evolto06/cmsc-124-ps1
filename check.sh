#!/usr/bin/env bash
# check.sh: Run the complete public automated check.
#
# This script runs each case in cases/. The repository contains all grading
# cases.
set -u

HARNESS_TAG="v1.1"
HARNESS_URL="https://raw.githubusercontent.com/WhiteLicorice/cmsc-124-harness/${HARNESS_TAG}/run_tests.py"

failures=0

banner() {
  printf '\n== %s ==\n' "$1"
}

banner "build"
if ! ./build.sh; then
  echo "check.sh: the build failed, so nothing else can run." >&2
  exit 1
fi

if command -v python3 >/dev/null 2>&1; then
  PYTHON=python3
elif command -v python >/dev/null 2>&1; then
  PYTHON=python
else
  echo "check.sh: Python 3.9 or newer is required." >&2
  exit 1
fi

if ! "$PYTHON" -c 'import sys; sys.exit(0 if sys.version_info >= (3, 9) else 1)'; then
  echo "check.sh: Python 3.9 or newer is required." >&2
  exit 1
fi

if [[ ! -f run_tests.py ]]; then
  banner "fetching harness ${HARNESS_TAG}"
  harness_tmp="$(mktemp ./run_tests.py.XXXXXX)"
  if ! curl -fsSL "$HARNESS_URL" -o "$harness_tmp"; then
    rm -f "$harness_tmp"
    echo "check.sh: could not fetch the harness. Check your network." >&2
    exit 1
  fi
  mv "$harness_tmp" run_tests.py
fi

banner "correctness"
if ! "$PYTHON" run_tests.py cases; then
  failures=1
fi

banner "sanitizers"
# AddressSanitizer and UndefinedBehaviorSanitizer detect memory faults.
# A standard output comparison cannot detect these faults.
#
# MinGW GCC does not include libasan or libubsan. This test cannot run on
# MSYS2. The GitHub Actions workflow runs this test on Linux and macOS.
probe_cc="${CC:-cc}"
if ! command -v "$probe_cc" >/dev/null 2>&1; then
  probe_cc="gcc"
fi

probe_dir="$(mktemp -d)"
printf 'int main(void){return 0;}\n' > "$probe_dir/probe.c"

if command -v "$probe_cc" >/dev/null 2>&1 &&
   "$probe_cc" -fsanitize=address,undefined -o "$probe_dir/probe" "$probe_dir/probe.c" >/dev/null 2>&1; then
  rm -rf "$probe_dir"
  cmake -E remove_directory build-san

  if ! cmake -S . -B build-san -G Ninja -DCMAKE_BUILD_TYPE=Debug -DDT_SANITIZE=ON >/dev/null; then
    echo "check.sh: sanitized configure failed." >&2
    failures=1
  elif ! cmake --build build-san >/dev/null; then
    echo "check.sh: sanitized build failed." >&2
    failures=1
  else
    # The harness reports a sanitizer abort as a failed test.
    #
    # Linux AddressSanitizer enables leak detection by default. Apple
    # AddressSanitizer does not include a leak checker.
    export ASAN_OPTIONS="abort_on_error=0"
    export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1"
    if ! DT_BUILD_DIR=./build-san "$PYTHON" run_tests.py cases; then
      failures=1
    fi
  fi
else
  rm -rf "$probe_dir"
  echo "SKIP: $probe_cc cannot link -fsanitize=address,undefined here."
  echo "SKIP: this is expected on MSYS2 and MinGW, which ship no libasan."
  echo "SKIP: the sanitized run happens on Linux, on macOS, and in CI."
fi

banner "result"
if [[ "$failures" -eq 0 ]]; then
  echo "All published checks passed."
else
  echo "Some published checks failed. Scroll up for the first one." >&2
fi
exit "$failures"
