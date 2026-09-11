#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="${TMPDIR:-/tmp}/it360-notify-target-$$"
mkdir -p "$TMP/xecore"
trap 'rm -rf "$TMP"' EXIT
cat > "$TMP/xecore/xboxkrnl.h" <<'H'
#pragma once
#include <stdint.h>
typedef void* HANDLE;
typedef void* HMODULE;
typedef uint32_t NTSTATUS;
#ifdef __cplusplus
extern "C" {
#endif
NTSTATUS NtClose(HANDLE);
void DbgPrint(const char*, ...);
#ifdef __cplusplus
}
#endif
H
cat > "$TMP/xecore/xam.h" <<'H'
#pragma once
H
g++ -std=c++98 -Wall -Wextra -Werror -Wno-unknown-pragmas \
    -DIT360_XBOX=1 -DIT360_OPENXECHAIN=1 \
    -I"$TMP" -I"$ROOT/src" -c "$ROOT/src/shared/notify.cpp" -o "$TMP/notify.o"
echo "OpenXeChain-only notification branch syntax: PASS"
