#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
PREFIX="${OPENXECHAIN_PREFIX:-${OXC_PREFIX:-$ROOT/.openxechain/sysroot}}"
BIN="$PREFIX/bin"

CC="${CC:-$BIN/clang}"
CXX="${CXX:-$BIN/clang++}"
SYNTHXEX="${SYNTHXEX:-$BIN/synthxex}"

OUT_NAME="iPhoneTether360-B9C-OXC"
BUILD="$ROOT/build"

for tool in "$CC" "$CXX" "$SYNTHXEX"; do
    if [[ ! -x "$tool" ]]; then
        echo "ERROR: OpenXeChain tool not found: $tool" >&2
        echo "Set OPENXECHAIN_PREFIX to the installed OpenXeChain sysroot." >&2
        exit 1
    fi
done

if [[ ! -f "$PREFIX/include/xecore/xboxkrnl.h" \
   || ! -f "$PREFIX/include/xecore/xam.h" \
   || ! -f "$PREFIX/lib/xecorelib.a" ]]; then
    echo "ERROR: incomplete OpenXeChain sysroot at $PREFIX" >&2
    exit 1
fi

python3 "$ROOT/tools/audit_openxechain_port.py"

rm -rf "$BUILD"
mkdir -p "$BUILD/obj"

SOURCES=(
    src/platform/xbox_platform.cpp
    src/plugin.cpp
    src/diag.cpp
    src/hook17559.cpp
    src/mux_proto.cpp
    src/lockdown_probe.cpp
    src/pair_identity.cpp
    src/pair_protocol.cpp
    src/pair_session.cpp
    src/base64.cpp
    src/sha256.cpp
    src/der_x509.cpp
    src/pair_store.cpp
    src/pair_crypto_provider.cpp
    src/usbmux_probe.cpp
    src/pair_link.cpp
    src/ipheth_ncm.cpp
    src/net_stack.cpp
    src/tether_driver.cpp
)

CXXFLAGS=(
    --target=ppc32-xbox360
    --sysroot="$PREFIX"
    -std=gnu++98
    -O2
    -DNDEBUG
    -DIT360_XBOX=1
    -DIT360_OPENXECHAIN=1
    -mlongcall
    -fno-exceptions
    -fno-rtti
    -fno-threadsafe-statics
    -ffunction-sections
    -fdata-sections
    -ferror-limit=0
    -Wall
    -Wextra
    -Werror
    -I"$ROOT/src"
)

OBJECTS=()
compile_failed=0

for source in "${SOURCES[@]}"; do
    object="$BUILD/obj/$(basename "${source%.cpp}").o"

    echo "================================================================"
    echo "CXX $source"

    if "$CXX" \
        "${CXXFLAGS[@]}" \
        -c "$ROOT/$source" \
        -o "$object"; then

        OBJECTS+=("$object")
    else
        compile_failed=1
        rm -f "$object"
        echo "FAILED $source"
    fi
done

if [[ $compile_failed -ne 0 ]]; then
    echo "================================================================"
    echo "ERROR: one or more iPhoneTether360 translation units failed."
    echo "All source files were attempted so the log contains every compiler error from this run."
    exit 1
fi

PE="$BUILD/$OUT_NAME.exe"
XEX="$BUILD/$OUT_NAME.xex"

echo "================================================================"
echo "LINK $PE"

"$CC" \
    --target=ppc32-xbox360 \
    --sysroot="$PREFIX" \
    -mlongcall \
    -fuse-ld=lld \
    "${OBJECTS[@]}" \
    -o "$PE" \
    -Wl,/dll \
    -Wl,/entry:_start \
    -Wl,/subsystem:xbox360 \
    -Wl,/base:0x91DE0000 \
    -Wl,/align:4096 \
    -Wl,/opt:ref \
    -Wl,/opt:icf

python3 "$ROOT/tools/verify_openxechain_pe.py" "$PE"

echo "SYNTHXEX $XEX"

"$SYNTHXEX" \
    --input "$PE" \
    --output "$XEX" \
    --type sysdll

python3 "$ROOT/tools/verify_openxechain_xex.py" "$XEX"

if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$XEX" | tee "$BUILD/$OUT_NAME.sha256"
fi

echo "Built $XEX"
