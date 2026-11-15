#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
# SPDX-License-Identifier: Apache-2.0
"""Decodes and encodes Tracegale messages offline. The format is specified in docs/protocol/README.md.

Input is untrusted, so decoding rejects anything the specification doesn't allow, and enforces its limits, instead of
trying to make sense of it. Uses only the standard library.

Usage: tracegale_wire.py [hex ...]     Decode each hex argument, or one hex message per line from stdin.
"""
import json
import sys

VERSION = 1
MAX_MESSAGE = 65535
MAX_DEPTH = 4
MAX_ITEMS = 256

TYPES = {
    1: "HELLO", 2: "ACK", 3: "ERROR", 4: "METRICS", 5: "LOGS",
    10: "XFER_START", 11: "XFER_CHUNK", 12: "XFER_STATUS",
    20: "IMG_CHECK", 21: "IMG_OFFER", 22: "IMG_GET", 23: "IMG_DATA", 24: "IMG_RESULT",
}

# Field names per message type, used only for display. Unknown fields stay visible under their number.
FIELDS = {
    "HELLO": {0: "fw_version", 1: "build_id", 2: "hardware", 3: "reset_reason", 4: "max_chunk"},
    "ACK": {0: "pending"},
    "ERROR": {0: "code", 1: "retry_after_s", 2: "detail"},
    "XFER_START": {0: "kind", 1: "total_len", 2: "sha256", 3: "build_id", 4: "flags"},
    "XFER_CHUNK": {0: "xfer_id", 1: "offset", 2: "data"},
    "XFER_STATUS": {0: "xfer_id", 1: "next_offset", 2: "chunk_max", 3: "complete"},
    "IMG_CHECK": {0: "fw_version", 1: "build_id", 2: "hardware"},
    "IMG_OFFER": {0: "image_ref", 1: "total_len", 2: "kind", 3: "base_sha256", 4: "fw_version", 5: "sha256"},
    "IMG_GET": {0: "image_ref", 1: "offset", 2: "len"},
    "IMG_DATA": {0: "image_ref", 1: "offset", 2: "data"},
    "IMG_RESULT": {0: "image_ref", 1: "outcome"},
    "METRICS": {0: "period_s", 1: "system", 2: "gauges", 3: "counters"},
    "LOGS": {0: "boot", 1: "uptime_ms", 2: "records", 3: "dropped"},
}

ERRORS = {
    1: "malformed", 2: "unsupported_version", 3: "unsupported_type", 4: "missing_field", 5: "too_large",
    6: "bad_offset", 7: "hash_mismatch", 8: "unknown_transfer", 9: "unknown_image", 10: "rate_limited",
    11: "not_authorized", 12: "server_busy", 13: "unsupported_kind",
}

# IMG_RESULT's outcome: how an update ended on the device.
OUTCOMES = {
    1: "installed", 2: "not_started", 3: "reverted", 4: "damaged", 5: "too_large", 6: "not_an_image", 7: "downgrade",
    8: "storage", 9: "unsupported_kind",
}


# LOGS' records: seq, uptime_ms, level, module, text.
LEVELS = {1: "error", 2: "warning", 3: "info", 4: "debug"}


def describe_record(record):
    if not isinstance(record, list) or len(record) != 5:
        return record
    seq, uptime_ms, level, module, text = record
    return {"seq": seq, "uptime_ms": uptime_ms, "level": f"{level} ({LEVELS.get(level, 'unknown')})", "module": module, "text": text}


# METRICS' system field: what the firmware measures by itself.
SYSTEM_METRICS = {
    1: "uptime_s", 2: "heap_free", 3: "heap_low", 4: "stack_low", 5: "cpu_load", 6: "net_sent", 7: "net_received",
    8: "checkin_failures", 9: "checkin_ms",
}


class WireError(ValueError):
    """The input isn't a valid message."""


def _head(data, pos):
    if pos >= len(data):
        raise WireError("truncated")
    first = data[pos]
    major, info = first >> 5, first & 0x1F
    pos += 1
    if info < 24:
        return major, info, pos
    if info > 27:
        raise WireError("indefinite lengths and reserved values aren't allowed")
    size = 1 << (info - 24)
    if pos + size > len(data):
        raise WireError("truncated")
    value = int.from_bytes(data[pos:pos + size], "big")
    # Each value has exactly one valid encoding, so two decoders can't disagree about a message.
    if value < (24 if size == 1 else 1 << (4 * size)):
        raise WireError("integer isn't in its shortest form")
    return major, value, pos + size


def _item(data, pos, depth):
    if depth > MAX_DEPTH:
        raise WireError("nested too deeply")
    major, value, pos = _head(data, pos)
    if major == 0:
        return value, pos
    if major == 1:
        return -1 - value, pos
    if major in (2, 3):
        if value > len(data) - pos:
            raise WireError("truncated")
        raw = data[pos:pos + value]
        pos += value
        if major == 2:
            return bytes(raw), pos
        try:
            return raw.decode("utf-8"), pos
        except UnicodeDecodeError as error:
            raise WireError("text isn't valid UTF-8") from error
    if major == 4:
        if value > MAX_ITEMS:
            raise WireError("too many items")
        items = []
        for _ in range(value):
            item, pos = _item(data, pos, depth + 1)
            items.append(item)
        return items, pos
    if major == 5:
        if value > MAX_ITEMS:
            raise WireError("too many items")
        result = {}
        last = -1
        for _ in range(value):
            key, pos = _item(data, pos, depth + 1)
            if type(key) is not int or key < 0:
                raise WireError("map keys must be unsigned integers")
            if key <= last:
                raise WireError("map keys must be unique and ascending")
            last = key
            result[key], pos = _item(data, pos, depth + 1)
        return result, pos
    if major == 6:
        raise WireError("tags aren't allowed")
    # Only false, true and null. Floats aren't used in version 1.
    if value in (20, 21):
        return value == 21, pos
    if value == 22:
        return None, pos
    raise WireError("floats and other simple values aren't allowed")


def decode(data):
    """Returns (version, type number, id, body), or raises WireError."""
    if len(data) > MAX_MESSAGE:
        raise WireError("message too large")
    message, end = _item(data, 0, 0)
    if end != len(data):
        raise WireError("bytes after the end of the message")
    if not isinstance(message, list) or len(message) != 4:
        raise WireError("a message is an array of 4 items")
    version, kind, ident, body = message
    if type(version) is not int or version < 1:
        raise WireError("bad version")
    if version != VERSION:
        raise WireError(f"unsupported version {version}")
    if type(kind) is not int or kind < 0:
        raise WireError("bad type")
    if type(ident) is not int or not 0 <= ident <= 0xFFFFFFFF:
        raise WireError("bad id")
    if not isinstance(body, dict):
        raise WireError("the body must be a map")
    return version, kind, ident, body


def _encode_head(major, value):
    if value < 24:
        return bytes([major << 5 | value])
    for info, size in ((24, 1), (25, 2), (26, 4), (27, 8)):
        if value < 1 << (8 * size):
            return bytes([major << 5 | info]) + value.to_bytes(size, "big")
    raise ValueError("integer too large")


def encode_item(item):
    if item is False or item is True:
        return bytes([0xF5 if item else 0xF4])
    if item is None:
        return b"\xf6"
    if isinstance(item, int):
        return _encode_head(0, item) if item >= 0 else _encode_head(1, -1 - item)
    if isinstance(item, bytes):
        return _encode_head(2, len(item)) + item
    if isinstance(item, str):
        raw = item.encode("utf-8")
        return _encode_head(3, len(raw)) + raw
    if isinstance(item, list):
        return _encode_head(4, len(item)) + b"".join(encode_item(i) for i in item)
    if isinstance(item, dict):
        parts = (encode_item(k) + encode_item(item[k]) for k in sorted(item))
        return _encode_head(5, len(item)) + b"".join(parts)
    raise TypeError(f"can't encode {type(item)}")


def encode(kind, ident, body):
    return encode_item([VERSION, kind, ident, body])


def describe(data):
    """Returns a readable dictionary for one message. Unknown types and fields are shown, not dropped."""
    version, kind, ident, body = decode(data)
    name = TYPES.get(kind, f"unknown({kind})")
    names = FIELDS.get(name, {})
    fields = {}
    for key, value in body.items():
        label = names.get(key, str(key))
        if name == "ERROR" and key == 0:
            value = f"{value} ({ERRORS.get(value, 'unknown')})"
        if name == "IMG_RESULT" and key == 1:
            value = f"{value} ({OUTCOMES.get(value, 'unknown')})"
        if name == "METRICS" and key == 1 and isinstance(value, dict):
            value = {SYSTEM_METRICS.get(k, str(k)): v for k, v in value.items()}
        if name == "LOGS" and key == 2 and isinstance(value, list):
            value = [describe_record(r) for r in value]
        if name == "METRICS" and key in (2, 3) and isinstance(value, list) and len(value) % 2 == 0:
            value = {str(value[i]): value[i + 1] for i in range(0, len(value), 2)}
        fields[label] = value.hex() if isinstance(value, bytes) else value
    return {"version": version, "type": name, "id": ident, "size": len(data), "fields": fields}


def main(args):
    lines = args if args else (line.strip() for line in sys.stdin)
    failed = False
    for line in lines:
        if not line:
            continue
        try:
            print(json.dumps(describe(bytes.fromhex(line))))
        except ValueError as error:
            print(json.dumps({"error": str(error)}))
            failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
