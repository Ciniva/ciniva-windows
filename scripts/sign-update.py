# Prints the base64 Ed25519 signature of a file. The private key (base64, raw 32 bytes) comes from UPDATE_SIGNING_KEY.
import base64
import os
import sys

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

if len(sys.argv) != 2 or not os.environ.get("UPDATE_SIGNING_KEY", "").strip():
    sys.exit("usage: UPDATE_SIGNING_KEY=... python sign-update.py <file>")

key = Ed25519PrivateKey.from_private_bytes(base64.b64decode(os.environ["UPDATE_SIGNING_KEY"].strip()))
with open(sys.argv[1], "rb") as f:
    print(base64.b64encode(key.sign(f.read())).decode())
