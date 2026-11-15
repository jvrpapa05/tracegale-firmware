#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
# SPDX-License-Identifier: Apache-2.0
"""Writes each fuzzer's starting inputs. Usage: make_corpus.py <wire|mqtt|identity> <folder>

wire:     the protocol's test vectors, valid and invalid.
mqtt:     a broker's side of good sessions: CONNACK, SUBACK, then replies on the device's topic, and a few variations.
identity: a real device certificate, like the server issues.
"""
import base64
import json
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[3]
target, out = sys.argv[1], Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=True)
vectors = json.loads((root / "docs" / "protocol" / "vectors.json").read_text())


def remaining(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        out.append(b | (0x80 if n else 0))
        if not n:
            return bytes(out)


def publish(topic, payload, qos=1, packet_id=7):
    var = len(topic).to_bytes(2, "big") + topic + (packet_id.to_bytes(2, "big") if qos else b"") + payload
    return bytes([0x30 | (qos << 1)]) + remaining(len(var)) + var


if target == "wire":
    for kind in ("valid", "invalid"):
        for vector in vectors[kind]:
            (out / f"{kind}-{vector['name']}").write_bytes(bytes.fromhex(vector["hex"]))
elif target == "mqtt":
    connack, suback = bytes([0x20, 2, 0, 0]), bytes([0x90, 3, 0, 1, 1])
    down = b"tg/v1/dev-0123456789abcdef/down"
    replies = [bytes.fromhex(v["hex"]) for v in vectors["valid"]][:6]
    for i, reply in enumerate(replies):
        (out / f"session-{i}").write_bytes(connack + suback + publish(down, reply) + publish(down, reply))
    (out / "puback-first").write_bytes(connack + suback + bytes([0x40, 2, 0, 1]) + publish(down, replies[0]))
    (out / "other-topic").write_bytes(connack + suback + publish(b"tg/v1/other/down", replies[0]) + publish(down, replies[0]))
    (out / "qos0").write_bytes(connack + suback + publish(down, replies[0], qos=0))
    (out / "refused").write_bytes(bytes([0x20, 2, 0, 5]))
    (out / "too-large").write_bytes(connack + suback + publish(down, bytes(300)))
elif target == "identity":
    # A certificate in the shape the server issues: ECDSA P-256, CN = the device's identity. Made for this corpus only; its
    # key was never kept.
    pem = (Path(__file__).parent / "device-cert.pem").read_text()
    der = base64.b64decode("".join(line for line in pem.splitlines() if not line.startswith("-----")))
    (out / "device-cert").write_bytes(der)
    (out / "truncated").write_bytes(der[: len(der) // 2])
else:
    sys.exit(f"unknown target {target}")
