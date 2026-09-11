#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="${TMPDIR:-/tmp}/it360-symbol-audit-$$"
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT
cd "$ROOT"

extract_set() {
    local name="$1"
    python3 - "$name" <<'PY'
from pathlib import Path
import re, sys
name=sys.argv[1]
s=Path('build_openxechain.sh').read_text()
m=re.search(r'^%s=\(\n(.*?)^\)'%re.escape(name),s,re.M|re.S)
if not m: raise SystemExit('missing source array '+name)
for line in m.group(1).splitlines():
    line=line.strip()
    if line and not line.startswith('#') and not line.startswith('src/tls/'):
        print(line)
PY
}

for target in LOADER_SOURCES LICENSEID_SOURCES CORE_SOURCES; do
    out="$TMP/$target"
    mkdir -p "$out"
    while IFS= read -r source; do
        object="$out/${source//\//_}.o"
        g++ -std=c++98 -Wall -Wextra -Werror -Wno-unknown-pragmas \
            -fno-exceptions -fno-rtti -fno-threadsafe-statics \
            -DIT360_XBOX=1 -Isrc -c "$source" -o "$object"
    done < <(extract_set "$target")
    ld -r "$out"/*.o -o "$TMP/$target.combined.o"

    unresolved="$TMP/$target.unresolved"
    nm -uC "$TMP/$target.combined.o" | sed -E 's/^ +U //' > "$unresolved"
    if [[ "$target" == CORE_SOURCES ]]; then
        # BearSSL-dependent Core TLS sources are deliberately unavailable before
        # the pinned XboxTLS checkout. Other project namespaces must resolve.
        if grep 'it360_' "$unresolved" | grep -v 'it360_tls::' > "$TMP/unexpected"; then
            echo "ERROR: unresolved non-TLS project symbols in Core:" >&2
            cat "$TMP/unexpected" >&2
            exit 1
        fi
    else
        if grep 'it360_' "$unresolved" > "$TMP/unexpected"; then
            echo "ERROR: unresolved project symbols in $target:" >&2
            cat "$TMP/unexpected" >&2
            exit 1
        fi
    fi
    echo "$target duplicate/project-symbol audit: PASS"
done
