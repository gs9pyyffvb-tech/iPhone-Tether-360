#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
PREFIX="${OPENXECHAIN_PREFIX:-${OXC_PREFIX:-$ROOT/.openxechain/sysroot}}"
BIN="$PREFIX/bin"
BUILD="$ROOT/build"
APP_VERSION="1.0.0"

CC="${CC:-$BIN/clang}"
CXX="${CXX:-$BIN/clang++}"
SYNTHXEX="${SYNTHXEX:-$BIN/synthxex}"
NM="${NM:-$BIN/llvm-nm}"

XBOXTLS_COMMIT="dc0cc21cbf6ae9e75f1974833742f80b45519326"
XBOXTLS_DIR="$BUILD/deps/XboxTLS"

for tool in "$CC" "$CXX" "$SYNTHXEX"; do
    if [[ ! -x "$tool" ]]; then
        echo "ERROR: OpenXeChain tool not found: $tool" >&2
        echo "Set OPENXECHAIN_PREFIX to the installed OpenXeChain sysroot." >&2
        exit 1
    fi
done

if [[ ! -f "$PREFIX/include/xecore/xboxkrnl.h" \
   || ! -f "$PREFIX/include/xecore/xam.h" \
   || ! -f "$PREFIX/lib/xecorelib.a" \
   || ! -f "$PREFIX/ppc-xbox360/lib/libc.a" \
   || ! -f "$PREFIX/lib/generic/libclang_rt.builtins-powerpc.a" ]]; then
    echo "ERROR: incomplete OpenXeChain sysroot at $PREFIX" >&2
    exit 1
fi

if [[ -f "$ROOT/tools/audit_openxechain_port.py" ]]; then
    python3 "$ROOT/tools/audit_openxechain_port.py"
fi
if [[ -f "$ROOT/tools/audit_step2.py" ]]; then
    python3 "$ROOT/tools/audit_step2.py"
fi
if [[ -f "$ROOT/tools/audit_step3.py" ]]; then
    python3 "$ROOT/tools/audit_step3.py"
fi
if [[ -f "$ROOT/tools/audit_build_manifest.py" ]]; then
    python3 "$ROOT/tools/audit_build_manifest.py"
fi
if [[ -f "$ROOT/tools/audit_step4.py" ]]; then
    python3 "$ROOT/tools/audit_step4.py"
fi
if [[ -f "$ROOT/tools/audit_steps36.py" ]]; then
    python3 "$ROOT/tools/audit_steps36.py"
fi
if [[ -f "$ROOT/tools/audit_steps79.py" ]]; then
    python3 "$ROOT/tools/audit_steps79.py"
fi
if [[ -f "$ROOT/tools/audit_step17.py" ]]; then
    python3 "$ROOT/tools/audit_step17.py"
fi
if [[ -f "$ROOT/tools/audit_final_polish.py" ]]; then
    python3 "$ROOT/tools/audit_final_polish.py"
fi

rm -rf "$BUILD"
mkdir -p "$BUILD/obj/core" "$BUILD/obj/loader" "$BUILD/obj/licenseid" "$BUILD/obj/bearssl" "$BUILD/obj/host" "$BUILD/deps"

if command -v g++ >/dev/null 2>&1; then
    echo "================================================================"
    echo "Running B9D host packet/bridge tests"
    g++ -std=gnu++98 -Wall -Wextra -Werror -I"$ROOT/src" \
        "$ROOT/tools/test_step17_frames.cpp" \
        "$ROOT/src/native_net/frame_translate.cpp" \
        -o "$BUILD/obj/host/test_step17_frames"
    "$BUILD/obj/host/test_step17_frames"

    g++ -std=gnu++98 -Wall -Wextra -Werror -I"$ROOT/src" \
        "$ROOT/tools/test_step17_bridge.cpp" \
        "$ROOT/src/native_net/xnet_bridge.cpp" \
        "$ROOT/src/native_net/frame_translate.cpp" \
        -o "$BUILD/obj/host/test_step17_bridge"
    "$BUILD/obj/host/test_step17_bridge"
fi

echo "================================================================"
echo "Preparing pinned BearSSL/XboxTLS source $XBOXTLS_COMMIT"
git clone -q https://github.com/JakobRangel/XboxTLS.git "$XBOXTLS_DIR"
git -C "$XBOXTLS_DIR" checkout -q "$XBOXTLS_COMMIT"
# This XboxTLS snapshot enables Unix time unconditionally. The 360 Core supplies
# BearSSL with KeQuerySystemTime explicitly, so prevent libc time() from becoming
# an implicit trust dependency.
sed -i 's/^#define BR_USE_UNIX_TIME   1$/#define BR_USE_UNIX_TIME   0/' "$XBOXTLS_DIR/SSL/config.h"

COMMON_CXXFLAGS=(
    --target=ppc32-xbox360
    -std=gnu++98
    -O2
    -DNDEBUG
    -DIT360_XBOX=1
    -DIT360_OPENXECHAIN=1
    -fno-exceptions
    -fno-rtti
    -fno-threadsafe-statics
    -ferror-limit=0
    -Wall
    -Wextra
    -Werror
    -I"$ROOT/src"
)

compile_cpp_set() {
    local target="$1"
    shift
    local source object stem
    local dir="${target,,}"
    local -n out_objects="${target}_OBJECTS"
    out_objects=()
    local failed=0

    for source in "$@"; do
        stem="${source//\//_}"
        stem="${stem%.cpp}"
        object="$BUILD/obj/$dir/$stem.o"
        echo "CXX [$target] $source"
        if "$CXX" "${COMMON_CXXFLAGS[@]}" -c "$ROOT/$source" -o "$object"; then
            out_objects+=("$object")
        else
            failed=1
            rm -f "$object"
            echo "FAILED [$target] $source"
        fi
    done

    if [[ $failed -ne 0 ]]; then
        echo "ERROR: one or more $target translation units failed." >&2
        exit 1
    fi
}

LOADER_SOURCES=(
    src/loader/loader_main.cpp
    src/shared/app_path.cpp
    src/shared/boot_log.cpp
    src/shared/notify.cpp
    src/platform/xbox_platform.cpp
)

LICENSEID_SOURCES=(
    src/licenseid/licenseid_main.cpp
    src/shared/app_path.cpp
    src/shared/notify.cpp
    src/platform/xbox_platform.cpp
    src/license/cpu_key.cpp
    src/license/license_id.cpp
    src/sha256.cpp
)

CORE_SOURCES=(
    src/entry.cpp
    src/early_trace.cpp
    src/platform/xbox_platform.cpp
    src/shared/app_path.cpp
    src/plugin.cpp
    src/diag.cpp
    src/shared/notify.cpp
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
    src/net_stack_access.cpp
    src/tether_driver.cpp
    src/native_net/frame_translate.cpp
    src/native_net/xnet_bridge.cpp
    src/license/cpu_key.cpp
    src/license/license_id.cpp
    src/license/license_document.cpp
    src/license/license_runtime.cpp
    src/tls/b9c_stream.cpp
    src/tls/sectigo_roots.cpp
    src/tls/bearssl_https.cpp
)

compile_cpp_set LOADER "${LOADER_SOURCES[@]}"
compile_cpp_set LICENSEID "${LICENSEID_SOURCES[@]}"

BEARSSL_CFLAGS=(
    --target=ppc32-xbox360
    -std=c99
    -O2
    -DNDEBUG
    -DBR_ENABLE_INTRINSICS=0
    -DBR_USE_UNIX_TIME=0
    -DBR_USE_URANDOM=0
    -DBR_USE_GETENTROPY=0
    -DBR_USE_WIN32_RAND=0
    -w
    -I"$XBOXTLS_DIR/SSL"
    -I"$XBOXTLS_DIR/SSL/inc"
)

BEARSSL_OBJECTS=()
while IFS= read -r source; do
    rel="${source#$XBOXTLS_DIR/}"
    case "$rel" in
        SSL/rand/sysrng.c|*pwr8*|*x86ni*|*sse2*|*pclmul*|*ctmul64*|*ctmulq*|*m64*|*i62*)
            continue
            ;;
    esac
    stem="${rel//\//_}"
    stem="${stem%.c}"
    object="$BUILD/obj/bearssl/$stem.o"
    echo "CC  [bearssl] $rel"
    "$CC" "${BEARSSL_CFLAGS[@]}" -c "$source" -o "$object"
    BEARSSL_OBJECTS+=("$object")
done < <(find "$XBOXTLS_DIR/SSL" -type f -name '*.c' | sort)

if [[ ${#BEARSSL_OBJECTS[@]} -eq 0 ]]; then
    echo "ERROR: no BearSSL sources were compiled" >&2
    exit 1
fi

# BearSSL headers are needed only by the Core TLS translation units.
COMMON_CXXFLAGS+=(
    -I"$XBOXTLS_DIR/SSL"
    -I"$XBOXTLS_DIR/SSL/inc"
)
compile_cpp_set CORE "${CORE_SOURCES[@]}"

if [[ -x "$NM" ]]; then
    echo "================================================================"
    echo "Core custom diagnostic entrypoint sanity check"
    ENTRY_OBJECT="$BUILD/obj/core/src_entry.o"
    if ! "$NM" "$ENTRY_OBJECT" | grep -E '[[:space:]]T[[:space:]]+_start$'; then
        echo "ERROR: custom diagnostic _start was not emitted by entry.cpp" >&2
        exit 1
    fi
fi

link_title() {
    local name="$1"
    shift
    local pe="$BUILD/$name.exe"
    local xex="$BUILD/$name.xex"
    echo "================================================================"
    echo "LINK title $pe"
    "$CXX" --target=ppc32-xbox360 "$@" -o "$pe" \
        -Wl,/subsystem:xbox360 \
        -Wl,/entry:_start \
        -Wl,/errorlimit:0
    if [[ -f "$ROOT/tools/verify_openxechain_pe.py" ]]; then
        python3 "$ROOT/tools/verify_openxechain_pe.py" "$pe" title
    fi
    echo "SYNTHXEX title $xex"
    "$SYNTHXEX" --input "$pe" --output "$xex" --type title
    if [[ -f "$ROOT/tools/verify_openxechain_xex.py" ]]; then
        python3 "$ROOT/tools/verify_openxechain_xex.py" "$xex" title
    fi
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$xex" > "$BUILD/$name.sha256"
    fi
}

link_title Loader "${LOADER_OBJECTS[@]}"
link_title LicenseID "${LICENSEID_OBJECTS[@]}"

CORE_PE="$BUILD/Core.exe"
CORE_XEX="$BUILD/Core.xex"
echo "================================================================"
echo "LINK sysdll $CORE_PE"
"$CXX" --target=ppc32-xbox360 \
    "${CORE_OBJECTS[@]}" \
    "${BEARSSL_OBJECTS[@]}" \
    -o "$CORE_PE" \
    -Wl,/subsystem:xbox360 \
    -Wl,/entry:_start \
    -Wl,/dll \
    -Wl,/base:0x91DE0000 \
    -Wl,/align:4096 \
    -Wl,/errorlimit:0

if [[ -f "$ROOT/tools/verify_openxechain_pe.py" ]]; then
    python3 "$ROOT/tools/verify_openxechain_pe.py" "$CORE_PE" sysdll
fi

echo "SYNTHXEX sysdll $CORE_XEX"
"$SYNTHXEX" --input "$CORE_PE" --output "$CORE_XEX" --type sysdll

if [[ -f "$ROOT/tools/verify_openxechain_xex.py" ]]; then
    python3 "$ROOT/tools/verify_openxechain_xex.py" "$CORE_XEX" sysdll
fi

if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$CORE_XEX" > "$BUILD/Core.sha256"
fi

cat > "$BUILD/BUILD_OUTPUTS.txt" <<TXT
iPhoneTether360 $APP_VERSION
Loader.xex
Core.xex
LicenseID.xex
TXT
printf '%s\n' "$APP_VERSION" > "$BUILD/VERSION.txt"

echo "================================================================"
echo "Built:"
echo "  $BUILD/Loader.xex"
echo "  $BUILD/Core.xex"
echo "  $BUILD/LicenseID.xex"
