#!/usr/bin/env python3
import sys
from cryptography import x509
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import padding

if len(sys.argv) != 4:
    raise SystemExit('usage: verify_device_cert.py ROOT_CERT DEVICE_CERT DEVICE_PUBLIC_KEY')
root = x509.load_pem_x509_certificate(open(sys.argv[1], 'rb').read())
dev = x509.load_pem_x509_certificate(open(sys.argv[2], 'rb').read())
expected_pub = serialization.load_pem_public_key(open(sys.argv[3], 'rb').read())
root.public_key().verify(dev.signature, dev.tbs_certificate_bytes, padding.PKCS1v15(), dev.signature_hash_algorithm)
assert dev.public_key().public_numbers() == expected_pub.public_numbers()
assert dev.serial_number > 0
assert dev.extensions.get_extension_for_class(x509.BasicConstraints).value.ca is False
ku = dev.extensions.get_extension_for_class(x509.KeyUsage).value
assert ku.digital_signature and ku.key_encipherment
assert len(dev.extensions.get_extension_for_class(x509.SubjectKeyIdentifier).value.digest) == 20
print('Batch 9B runtime DeviceCertificate signature/key/extensions verification: PASS')
