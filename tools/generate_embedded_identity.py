from pathlib import Path
from datetime import datetime, timezone, timedelta
import uuid, struct
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'src' / 'pair_identity_generated.h'

def bswap64_chunks_be(n: int, size: int) -> bytes:
    raw_le = n.to_bytes(size, 'little')
    return b''.join(raw_le[i:i+8][::-1] for i in range(0, size, 8))

def mod_inv_2_64(n: int) -> int:
    return pow(1 << 64, -1, n) & ((1 << 64) - 1)

def xecrypt_private_blob(k: rsa.RSAPrivateKey) -> bytes:
    nums = k.private_numbers()
    pub = nums.public_numbers
    cb = k.key_size // 8
    h = cb // 2
    cqw = cb // 8
    n = bswap64_chunks_be(pub.n, cb)
    p = bswap64_chunks_be(nums.p, h)
    q = bswap64_chunks_be(nums.q, h)
    dp = bswap64_chunks_be(nums.dmp1, h)
    dq = bswap64_chunks_be(nums.dmq1, h)
    cr = bswap64_chunks_be(nums.iqmp, h)
    blob = struct.pack('>IIQ', cqw, pub.e, mod_inv_2_64(pub.n)) + n+p+q+dp+dq+cr
    assert len(blob) == 0x390
    return blob

def c_array(name: str, data: bytes, align=False) -> str:
    chunks = []
    for i in range(0, len(data), 16):
        chunks.append('    ' + ','.join('0x%02X' % b for b in data[i:i+16]))
    pre = 'IT360_ALIGN8 ' if align else ''
    return f'{pre}static const unsigned char {name}[{len(data)}] = {{\n' + ',\n'.join(chunks) + '\n};\n'

def c_string(name: str, s: str) -> str:
    parts=[]
    for line in s.splitlines(True):
        esc=line.replace('\\','\\\\').replace('"','\\"').replace('\r','\\r').replace('\n','\\n')
        parts.append(f'    "{esc}"')
    return f'static const char {name}[] =\n' + '\n'.join(parts) + ';\n'

root_key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
host_key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
empty = x509.Name([])
now = datetime.now(timezone.utc)
nb = now - timedelta(days=1)
na = now + timedelta(days=3650)
root_cert = (x509.CertificateBuilder().subject_name(empty).issuer_name(empty)
    .public_key(root_key.public_key()).serial_number(1)
    .not_valid_before(nb).not_valid_after(na)
    .add_extension(x509.BasicConstraints(ca=True,path_length=None),critical=True)
    .sign(root_key, hashes.SHA256()))
host_cert = (x509.CertificateBuilder().subject_name(empty).issuer_name(empty)
    .public_key(host_key.public_key()).serial_number(1)
    .not_valid_before(nb).not_valid_after(na)
    .add_extension(x509.BasicConstraints(ca=False,path_length=None),critical=True)
    .add_extension(x509.KeyUsage(digital_signature=True,content_commitment=False,key_encipherment=True,
        data_encipherment=False,key_agreement=False,key_cert_sign=False,crl_sign=False,
        encipher_only=None,decipher_only=None),critical=True)
    .sign(root_key, hashes.SHA256()))

root_cert_pem=root_cert.public_bytes(serialization.Encoding.PEM).decode('ascii')
host_cert_pem=host_cert.public_bytes(serialization.Encoding.PEM).decode('ascii')
root_key_pem=root_key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()).decode('ascii')
host_key_pem=host_key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()).decode('ascii')
root_blob=xecrypt_private_blob(root_key)
host_blob=xecrypt_private_blob(host_key)

# Fixed identifier source bytes ensure HostID/SystemBUID remain stable across reconnects.
host_uuid = uuid.uuid4()
system_uuid = uuid.uuid4()

text='''#pragma once\n#include "platform/xbox_platform.h"\n// AUTO-GENERATED pairing identity for this build.\n// IMPORTANT: keep this file stable after an iPhone has trusted the Xbox.\n// Regenerating it intentionally creates a new host identity and requires pairing again.\nnamespace pair_identity_generated {\n'''
text += c_array('kRootPrivateXeCrypt', root_blob, True)
text += c_array('kHostPrivateXeCrypt', host_blob, True)
text += c_array('kHostUuidBytes', host_uuid.bytes)
text += c_array('kSystemBuidUuidBytes', system_uuid.bytes)
text += c_string('kRootPrivateKeyPem', root_key_pem)
text += c_string('kHostPrivateKeyPem', host_key_pem)
text += c_string('kRootCertificatePem', root_cert_pem)
text += c_string('kHostCertificatePem', host_cert_pem)
text += '} // namespace pair_identity_generated\n'
OUT.write_text(text)
print(OUT)
print('HostID seed', str(host_uuid).upper())
print('SystemBUID seed', str(system_uuid).upper())
