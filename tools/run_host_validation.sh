#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="${TMPDIR:-/tmp}/it360-oxc-validate-$$"
mkdir -p "$TMP"
trap 'rm -rf "$TMP" /tmp/b9b-device.pem /tmp/b9b-root.pem' EXIT
cd "$ROOT"

CXX98=(g++ -std=c++98 -Wall -Wextra -Werror -pedantic -Isrc)

"${CXX98[@]}" tests/test_ipheth_ncm.cpp src/ipheth_ncm.cpp -o "$TMP/test_ncm"
"$TMP/test_ncm"

"${CXX98[@]}" tests/test_net_stack.cpp src/net_stack.cpp -o "$TMP/test_net"
"$TMP/test_net"

"${CXX98[@]}" tests/test_pair_session.cpp src/pair_session.cpp src/pair_protocol.cpp src/pair_identity.cpp src/base64.cpp -o "$TMP/test_pair_session"
"$TMP/test_pair_session"

"${CXX98[@]}" tests/test_pair_complete.cpp src/pair_session.cpp src/pair_protocol.cpp src/pair_identity.cpp src/base64.cpp src/pair_store.cpp src/platform/xbox_platform.cpp -o "$TMP/test_pair_complete"
"$TMP/test_pair_complete"

python3 tools/verify_generated_identity.py
openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out "$TMP/device-key.pem" >/dev/null 2>&1
openssl pkey -in "$TMP/device-key.pem" -pubout -out "$TMP/device-pub.pem" >/dev/null 2>&1
g++ -std=c++17 -Wall -Wextra -Werror -Isrc tools/test_x509.cpp src/der_x509.cpp src/sha256.cpp src/base64.cpp -lcrypto -o "$TMP/test_x509"
"$TMP/test_x509" "$TMP/device-pub.pem"
python3 tools/verify_device_cert.py /tmp/b9b-root.pem /tmp/b9b-device.pem "$TMP/device-pub.pem"

echo "Running B9D frame/bridge host tests"
"${CXX98[@]}" tools/test_step17_frames.cpp src/native_net/frame_translate.cpp -o "$TMP/test_step17_frames"
"$TMP/test_step17_frames"
"${CXX98[@]}" tools/test_step17_bridge.cpp src/native_net/xnet_bridge.cpp src/native_net/frame_translate.cpp -o "$TMP/test_step17_bridge"
"$TMP/test_step17_bridge"

mkdir -p "$TMP/xbox"
while IFS= read -r f; do
    case "$f" in
        src/tls/*) continue ;;
    esac
    rel="${f#src/}"
    obj="${rel//\//_}"
    obj="${obj%.cpp}.o"
    g++ -std=c++98 -Wall -Wextra -Werror -Wno-unknown-pragmas -DIT360_XBOX=1 -Isrc -c "$f" -o "$TMP/xbox/$obj"
done < <(find src -type f -name '*.cpp' | sort)
echo "All non-BearSSL project translation units IT360_XBOX C++98 host syntax: PASS"

bash tools/audit_host_symbols.sh
bash tools/test_notify_openxechain_syntax.sh

python3 tools/audit_build_manifest.py
python3 tools/audit_step4.py
python3 tools/audit_openxechain_port.py
python3 tools/verify_openxechain_source.py
python3 tools/audit_step2.py
python3 tools/audit_step3.py
python3 tools/audit_step17.py
python3 tools/audit_final_polish.py

echo "iPhoneTether360 OpenXeChain host/source validation: PASS"
