#!/usr/bin/env python3
from pathlib import Path
import ast, re, struct, sys
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa

ROOT = Path(__file__).resolve().parents[1]
HEADER = (ROOT / 'src' / 'pair_identity_generated.h').read_text()


def c_string(name):
    m = re.search(r'static const char\s+' + re.escape(name) + r'\[\]\s*=\s*\n(.*?);', HEADER, re.S)
    if not m:
        raise AssertionError('missing ' + name)
    parts = re.findall(r'"(?:\\.|[^"\\])*"', m.group(1))
    return ''.join(ast.literal_eval(p) for p in parts).encode('ascii')


def c_array(name):
    m = re.search(r'static const unsigned char\s+' + re.escape(name) + r'\[(\d+)\]\s*=\s*\{(.*?)\};', HEADER, re.S)
    if not m:
        raise AssertionError('missing ' + name)
    data = bytes(int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{2})', m.group(2)))
    assert len(data) == int(m.group(1))
    return data


def qwne_to_int(data):
    assert len(data) % 8 == 0
    le = b''.join(data[i:i+8][::-1] for i in range(0, len(data), 8))
    return int.from_bytes(le, 'little')


def check_blob(blob, key):
    nums = key.private_numbers()
    pub = nums.public_numbers
    assert len(blob) == 0x390
    cqw, exponent, inv = struct.unpack('>IIQ', blob[:16])
    assert cqw == 32
    assert exponent == pub.e == 65537
    assert inv == (pow(1 << 64, -1, pub.n) & ((1 << 64) - 1))
    assert qwne_to_int(blob[0x10:0x110]) == pub.n
    assert qwne_to_int(blob[0x110:0x190]) == nums.p
    assert qwne_to_int(blob[0x190:0x210]) == nums.q
    assert qwne_to_int(blob[0x210:0x290]) == nums.dmp1
    assert qwne_to_int(blob[0x290:0x310]) == nums.dmq1
    assert qwne_to_int(blob[0x310:0x390]) == nums.iqmp


root_key = serialization.load_pem_private_key(c_string('kRootPrivateKeyPem'), password=None)
host_key = serialization.load_pem_private_key(c_string('kHostPrivateKeyPem'), password=None)
root_cert = x509.load_pem_x509_certificate(c_string('kRootCertificatePem'))
host_cert = x509.load_pem_x509_certificate(c_string('kHostCertificatePem'))
assert isinstance(root_key, rsa.RSAPrivateKey) and isinstance(host_key, rsa.RSAPrivateKey)
assert root_key.key_size == host_key.key_size == 2048
assert root_key.public_key().public_numbers() == root_cert.public_key().public_numbers()
assert host_key.public_key().public_numbers() == host_cert.public_key().public_numbers()
root_key.public_key().verify(root_cert.signature, root_cert.tbs_certificate_bytes, padding.PKCS1v15(), root_cert.signature_hash_algorithm)
root_key.public_key().verify(host_cert.signature, host_cert.tbs_certificate_bytes, padding.PKCS1v15(), host_cert.signature_hash_algorithm)
assert root_cert.extensions.get_extension_for_class(x509.BasicConstraints).value.ca is True
assert host_cert.extensions.get_extension_for_class(x509.BasicConstraints).value.ca is False
ku = host_cert.extensions.get_extension_for_class(x509.KeyUsage).value
assert ku.digital_signature and ku.key_encipherment
check_blob(c_array('kRootPrivateXeCrypt'), root_key)
check_blob(c_array('kHostPrivateXeCrypt'), host_key)
print('Batch 9B embedded identity + XeCrypt blob verification: PASS')
