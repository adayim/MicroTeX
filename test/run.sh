#!/usr/bin/env bash
# The checks, as CI runs them (.github/workflows/test.yml):
#
#   test/run.sh test [BIDI]   the tests, under AddressSanitizer and UBSan
#                             (BIDI: ON or OFF, default ON)
#   test/run.sh fuzz [SECS]   both libFuzzer targets over test/fuzz/corpus,
#                             SECS each (default 60); needs clang
#   test/run.sh strict        our sources under -Wextra -Wpedantic
#
# CXX picks the compiler; BUILD is where the builds go (default build/).
# A fuzzer's findings are saved in $BUILD/fuzz-artifacts.

set -euo pipefail

ROOT="$(cd -- "$(dirname -- "$0")/.." && pwd)"
BUILD="${BUILD:-$ROOT/build}"
JOBS="$(nproc 2>/dev/null || echo 4)"
what="${1:-test}"

case "$what" in
test)
  bidi="${2:-ON}"
  cxx="${CXX:-c++}"
  b="$BUILD/test-$(basename "$cxx")-bidi-$bidi"
  cmake -S "$ROOT" -B "$b" -DCMAKE_CXX_COMPILER="$cxx" -DMICROTEX_TESTS=ON \
    -DMICROTEX_BIDI="$bidi" \
    -DCMAKE_CXX_FLAGS="-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=undefined"
  cmake --build "$b" -j "$JOBS"
  (cd "$b" && ctest --output-on-failure)
  ;;

fuzz)
  secs="${2:-60}"
  cxx="${CXX:-clang++}"
  b="$BUILD/fuzz"
  cmake -S "$ROOT" -B "$b" -DCMAKE_CXX_COMPILER="$cxx" -DMICROTEX_FUZZ=ON \
    -DCMAKE_CXX_FLAGS="-O1 -g -fno-omit-frame-pointer -fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=undefined"
  cmake --build "$b" -j "$JOBS"
  art="$BUILD/fuzz-artifacts"
  mkdir -p "$art"
  # libFuzzer adds what it finds to the corpus it is given: work on a copy.
  for target in front_fuzz engine_fuzz; do
    corpus="$BUILD/fuzz-corpus/$target"
    mkdir -p "$corpus"
    cp "$ROOT"/test/fuzz/corpus/* "$corpus"/
    echo "== $target, $secs s"
    # -timeout: a runaway macro runs until the expander's caps stop it
    # (1 MB of expansion), up to about 15 s under the sanitizers.
    MICROTEX_FUZZ_OTF="${MICROTEX_FUZZ_OTF:-$ROOT/res/firamath/FiraMath-Regular.otf}" \
      "$b/test/$target" -max_total_time="$secs" -max_len=4096 -timeout=30 \
      -rss_limit_mb=2048 -print_final_stats=1 -artifact_prefix="$art/$target-" "$corpus"
  done
  ;;

strict)
  # The fork's own files. Upstream MicroTeX has known sign-compare and
  # missing-initializer warnings; they are not ours to hold to this.
  cd "$ROOT"
  ours=(
    lib/graphic/graphic_recorder.cpp lib/otf/otf_math_reader.cpp lib/utils/bidi.cpp
    lib/atom/font_family_atom.cpp lib/atom/mark_atom.cpp lib/atom/image_atom.cpp
    lib/front/*.cpp test/*.cpp test/fuzz/*.cpp
  )
  cxx="${CXX:-c++}"
  flags="$(pkg-config --cflags freetype2)"
  # The FriBidi branch of bidi.cpp, not only the #else without it.
  if pkg-config --exists fribidi; then flags="$flags $(pkg-config --cflags fribidi) -DHAVE_FRIBIDI"; fi
  log="$(mktemp)"
  trap 'rm -f "$log"' EXIT
  # microtexconfig.h is made by the build; the version is all it holds.
  cfg="$(mktemp -d)"
  printf '#pragma once\n#define MICROTEX_VERSION_MAJOR 0\n#define MICROTEX_VERSION_MINOR 0\n#define MICROTEX_VERSION_PATCH 0\n' \
    > "$cfg/microtexconfig.h"
  # shellcheck disable=SC2086
  "$cxx" -std=c++17 -Wextra -Wpedantic -fsyntax-only -Ilib -I"$cfg" -DGLYPH_RENDER_TYPE=0 \
    $flags "${ours[@]}" > "$log" 2>&1 || true
  rm -rf "$cfg"
  # Warnings in our own files (and our headers) only.
  if grep -E "^(lib/(graphic/graphic_recorder|otf/otf_math_reader|utils/bidi|atom/(font_family|mark|image)_atom)\.(cpp|h)|lib/front/[^:]*|lib/macro/macro_args\.h|test/[^:]*):[0-9]+:[0-9]+: (warning|error)" "$log"; then
    echo "strict: warnings in our own code (above)" >&2
    exit 1
  fi
  if grep -q "error:" "$log"; then
    cat "$log" >&2
    echo "strict: did not compile" >&2
    exit 1
  fi
  echo "strict: our sources are warning-clean under -Wextra -Wpedantic"
  ;;

*)
  echo "usage: test/run.sh test [ON|OFF] | fuzz [SECS] | strict" >&2
  exit 2
  ;;
esac
