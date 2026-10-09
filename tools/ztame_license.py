#!/usr/bin/env python3
"""
ZTame license-key producer / verifier  (Ed25519)

Offline, deterministic, self-contained license code-generation for ZTame.

Scheme
------
  * Curve/algo : Ed25519 (EdDSA / RFC 8032; deterministic signature)
  * Payload    : version(1 byte) | len2(customer)  customer
                 | len2(order_ref) order_ref | len2(issue) issue
                 | len2(expiry) expiry
                 Each of the 4 explicit fields is uint16-big-endian-length
                 prefixed (TLV-lite).
  * Signature  : Ed25519 (detached) over the EXACT payload bytes.
  * License key: base64url( payload_bytes || 64-byte_signature ), no padding.
  * Deterministic: fixed (customer, order_ref, issue, expiry) + private key
                   ALWAYS produce the same key.

Commands
--------
  genkey        <priv_path> <pub_path>
  produce       --customer <name> --order-ref <ref>
                [--issue YYYY-MM-DD] [--expiry perpetual|YYYY-MM-DD]
                --priv <priv_path>
  verify        --key <license_key> --pub <pub_path>
  fingerprint   --pub <pub_path>

The PRIVATE key never leaves this host (files under tools/keys/ are gitignored).
The PUBLIC key is embedded in the ZTame plugin at build time for offline
license validation.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import sys
from datetime import date

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import (
    Ed25519PrivateKey,
    Ed25519PublicKey,
)

FORMAT_VERSION = b"\x01"


def b64u(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode("ascii")


def unb64u(s: str) -> bytes:
    pad = "=" * (-len(s) % 4)
    return base64.urlsafe_b64decode(s + pad)


def enc_field(value: str) -> bytes:
    raw = value.encode("utf-8")
    if len(raw) > 0xFFFF:
        raise ValueError("field too long (>65535 bytes)")
    return len(raw).to_bytes(2, "big") + raw


def dec_field(buf: bytes, off: int):
    if off + 2 > len(buf):
        raise ValueError("truncated field length")
    n = int.from_bytes(buf[off:off + 2], "big")
    start = off + 2
    if start + n > len(buf):
        raise ValueError("truncated field payload")
    return buf[start:start + n].decode("utf-8"), start + n


def build_payload(customer: str, order_ref: str, issue: str, expiry: str) -> bytes:
    return (FORMAT_VERSION + enc_field(customer) + enc_field(order_ref)
            + enc_field(issue) + enc_field(expiry))


def parse_payload(payload: bytes) -> dict:
    if not payload or payload[:1] != FORMAT_VERSION:
        raise ValueError("unsupported blob format version")
    off = 1
    customer, off = dec_field(payload, off)
    order_ref, off = dec_field(payload, off)
    issue, off = dec_field(payload, off)
    expiry, off = dec_field(payload, off)
    if off != len(payload):
        raise ValueError("trailing bytes in payload")
    return {
        "version": FORMAT_VERSION[0],
        "customer": customer,
        "order_ref": order_ref,
        "issue": issue,
        "expiry": expiry,
    }


def make_license_key(payload: bytes, sig: bytes) -> str:
    return b64u(payload + sig)


def split_license_key(license_key: str) -> tuple[bytes, bytes]:
    blob = unb64u(license_key)
    if len(blob) < 65:
        raise ValueError("license key too short")
    return blob[:-64], blob[-64:]


def public_key_bytes(pub: Ed25519PublicKey) -> bytes:
    return pub.public_bytes(encoding=serialization.Encoding.DER,
                            format=serialization.PublicFormat.SubjectPublicKeyInfo)


def fingerprint(pub: Ed25519PublicKey) -> str:
    return hashlib.sha256(public_key_bytes(pub)).hexdigest()


def load_private(path: str) -> Ed25519PrivateKey:
    with open(path, "rb") as f:
        return serialization.load_pem_private_key(f.read(), password=None)


def load_public(path: str) -> Ed25519PublicKey:
    with open(path, "rb") as f:
        data = f.read()
    try:
        return serialization.load_der_public_key(data)
    except Exception:
        return serialization.load_pem_public_key(data)


# --------------------------------------------------------------------------- #
# Commands
# --------------------------------------------------------------------------- #
def cmd_genkey(args):
    priv = Ed25519PrivateKey.generate()
    pub = priv.public_key()
    with open(args.priv, "wb") as f:
        f.write(priv.private_bytes(
            encoding=serialization.Encoding.PEM,
            format=serialization.PrivateFormat.PKCS8,
            encryption_algorithm=serialization.NoEncryption()))
    with open(args.pub, "wb") as f:
        f.write(pub.public_bytes(
            encoding=serialization.Encoding.PEM,
            format=serialization.PublicFormat.SubjectPublicKeyInfo))
    print("private key  ->", args.priv)
    print("public  key  ->", args.pub)
    print("public  fp  ->", fingerprint(pub))
    return 0


def cmd_produce(args):
    priv = load_private(args.priv)
    pub = priv.public_key()
    issue = args.issue or date.today().isoformat()
    expiry = args.expiry or "perpetual"
    payload = build_payload(args.customer, args.order_ref, issue, expiry)
    sig = priv.sign(payload)
    key = make_license_key(payload, sig)
    pub.verify(sig, payload)  # immediate self-check
    print(json.dumps({
        "license_key": key,
        "payload": parse_payload(payload),
        "verified": True,
    }, indent=2))
    return 0


def cmd_verify(args):
    pub = load_public(args.pub)
    payload, sig = split_license_key(args.key)
    info = parse_payload(payload)
    try:
        pub.verify(sig, payload)
        print(json.dumps({"valid": True, "payload": info,
                          "public_fingerprint": fingerprint(pub)}, indent=2))
        return 0
    except Exception as e:
        print(json.dumps({"valid": False, "payload": info,
                          "error": str(e),
                          "public_fingerprint": fingerprint(pub)}, indent=2))
        return 1


def cmd_fingerprint(args):
    print(fingerprint(load_public(args.pub)))
    return 0


def main(argv=None) -> int:
    p = argparse.ArgumentParser(prog="ztame_license",
                                description="ZTame Ed25519 license producer/verifier")
    sub = p.add_subparsers(dest="cmd", required=True)

    g = sub.add_parser("genkey")
    g.add_argument("priv")
    g.add_argument("pub")
    g.set_defaults(func=cmd_genkey)

    pr = sub.add_parser("produce")
    pr.add_argument("--customer", required=True)
    pr.add_argument("--order-ref", required=True)
    pr.add_argument("--issue")
    pr.add_argument("--expiry")
    pr.add_argument("--priv", required=True)
    pr.set_defaults(func=cmd_produce)

    v = sub.add_parser("verify")
    v.add_argument("--key", required=True)
    v.add_argument("--pub", required=True)
    v.set_defaults(func=cmd_verify)

    f = sub.add_parser("fingerprint")
    f.add_argument("--pub", required=True)
    f.set_defaults(func=cmd_fingerprint)

    args = p.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
