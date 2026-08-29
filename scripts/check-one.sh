#!/usr/bin/env bash
# Compile-check one src/ file against BOTH standard libraries:
#   1. production: arm64 + vendored libcxx + release flags
#   2. host:       x86_64 MSVC STL
# Usage: bash scripts/check-one.sh src/manager/custom_chart/AffNormalizer.cpp
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1
# Not under build/: build.ps1 wipes that tree, which used to make a long sweep
# fail halfway through with "unable to open output file". The directory is
# re-created before every compile so a concurrent build cannot strand it either.
OUT_DIR=".tmp/check-one"

# Toolchain layout is discovered, not hard-coded: these used to be literal paths
# and broke on any machine where the NDK, VS or the Windows SDK sat on a
# different drive or carried a different version number (the MSVC and SDK
# version directories change with every toolchain update). tests/toolchain_probe.py
# is shared with the Python test runners so there is only one place to fix.
PYTHON="${PYTHON:-}"
if [[ -z $PYTHON ]]; then
  for candidate in python python3; do
    if command -v "$candidate" >/dev/null 2>&1; then PYTHON=$candidate; break; fi
  done
fi
if [[ -z $PYTHON ]]; then
  echo "check-one.sh needs python to locate the toolchain; set \$PYTHON." >&2
  exit 1
fi
probe() { "$PYTHON" "tests/toolchain_probe.py" "$1"; }
# Each probe prints its own "set <VARIABLE>" hint on failure, so just stop here.
NDK_CXX="$(probe ndk-clang)" || exit 1
HOST_CXX="$(probe msvc-clang)" || exit 1
MSVC_INC="$(probe msvc-include)" || exit 1
SDK_INC="$(probe sdk-include)" || exit 1
ZLIB_INC="$(probe zlib-include)" || exit 1
# Same extra include dirs check_production_conformance() uses, so a file that
# passes here also passes the full-source sweep in tests/run_host_tests.py.
CONF_INC=(-I third_party/lsplt/lsplt/src/main/jni/include
          -I third_party/shadowhook/shadowhook/src/main/cpp/include)
INC=(-I src -I third_party/magic_enum/include -I third_party/json/include -I third_party
     "${CONF_INC[@]}")
# tests/stubs only for the host build: its unistd.h would shadow the NDK's on
# the production compile ( -I is searched ahead of the sysroot ) and hide getpid.
HOST_INC=(-I tests/stubs "${INC[@]}")

# Files the MSVC host cannot compile. Audited one by one on 2026-08-29: the list
# used to hold 17 entries, 11 of which compiled fine and were losing host
# verification for no reason. Keep a reason next to every entry and re-audit
# whenever the toolchain moves.
HOST_SKIP=(
  # Android NDK headers: they assume a versioned ELF target and Bionic typedefs
  # such as off64_t, so nothing can satisfy them under an MSVC target.
  "src/features/AssetVirtualizer.cpp"      # android/asset_manager.h
  "src/manager/GameVersionManager.cpp"     # jni.h (via utils/JniUtils.hpp)
  "src/wrapper/JniEntryWrapper.cpp"        # jni.h
  "src/wrapper/ZygiskEntryWrapper.cpp"     # jni.h
  "src/utils/memory/ShadowHookAdapter.cpp" # dlfcn.h
  # Not Android at all: MSVC STL rejects std::atomic_load_explicit on a
  # shared_ptr (C++20 deprecated it, STL4029) and -Werror promotes that to a
  # failure the production build never sees. Do NOT "fix" it by switching to
  # std::atomic<std::shared_ptr<...>> -- vendored libcxx has no such
  # specialization (verified 2026-08-29: "_Atomic cannot be applied to
  # shared_ptr"), so the free functions are the only spelling that compiles on
  # both sides. Production-checked only.
)
host_skipped() {
  local file=$1 candidate
  for candidate in "${HOST_SKIP[@]}"; do
    [[ $file == "$candidate" ]] && return 0
  done
  return 1
}

status=0
for file in "$@"; do
  mkdir -p "$OUT_DIR"
  # -include vector in the production compile mirrors check_production_conformance():
  # rapidyaml's std_fwd.hpp detects the standard library from it plus the ABI
  # namespace define, and -nostdinc++ implies neither. Measured inert on all 37
  # sources today, but this script is meant to be a faithful proxy for the
  # authoritative sweep, so the flag sets stay in step.
  #
  # Keep comments out of the backslash-continued command below: a '#' line inside
  # one terminates the logical line, so every argument after it would be dropped
  # (and executed as a separate command) while `bash -n` still reports success.
  if output=$("$NDK_CXX" --target=aarch64-linux-android21 -std=c++23 -fno-exceptions -fno-rtti \
      -fvisibility=hidden -fvisibility-inlines-hidden -Wall -Wextra -Werror -O2 -c \
      -nostdinc++ -isystem third_party/libcxx/include \
      -D_LIBCPP_NO_EXCEPTIONS -D_LIBCPP_NO_RTTI -D_LIBCPP_ABI_NAMESPACE=_LIBCPP_ABI_NAMESPACE \
      -DJSON_NOEXCEPTION -DC4_NO_DEBUG_BREAK -DC4_USE_ASSERT=0 \
      -include vector \
      "${INC[@]}" \
      -isystem third_party/rapidyaml/src -isystem third_party/rapidyaml/ext/c4core.src \
      "$file" -o "$OUT_DIR/prod.o" 2>&1); then
    echo "prod OK   $file"
  else
    echo "prod FAIL $file"; echo "$output" | head -30; status=1
  fi
  if host_skipped "$file"; then
    echo "host SKIP $file (android-only)"
    continue
  fi
  if output=$("$HOST_CXX" -std=c++23 -O2 -Wall -Wextra -Werror \
      -target x86_64-pc-windows-msvc \
      -isystem "$MSVC_INC" -isystem "$SDK_INC/ucrt" -isystem "$SDK_INC/um" -isystem "$SDK_INC/shared" \
      -isystem "$ZLIB_INC" \
      -D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_WARNINGS \
      -DSTBIR_NO_SIMD -DSTBI_NO_SIMD \
      "${HOST_INC[@]}" \
      -isystem third_party/rapidyaml/src -isystem third_party/rapidyaml/ext/c4core.src \
      -c "$file" -o "$OUT_DIR/host.o" 2>&1); then
    echo "host OK   $file"
  else
    echo "host FAIL $file"; echo "$output" | head -30; status=1
  fi
done
exit $status
