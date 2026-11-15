#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
# SPDX-License-Identifier: Apache-2.0
"""Converts docs/protocol/vectors.json into C tables for the firmware's tests."""
import json
import sys


def table(name, vectors):
    lines = [f"static const struct vector {name}[] = {{"]
    for v in vectors:
        data = bytes.fromhex(v["hex"])
        body = ", ".join(f"0x{b:02x}" for b in data) or "0"
        lines.append(f'\t{{{json.dumps(v["name"])}, (const uint8_t[]){{{body}}}, {len(data)}}},')
    lines.append("};")
    return "\n".join(lines)


def main():
    with open(sys.argv[1], encoding="utf-8") as f:
        doc = json.load(f)
    with open(sys.argv[2], "w", encoding="utf-8") as f:
        f.write(table("valid", doc["valid"]) + "\n" + table("invalid", doc["invalid"]) + "\n")


if __name__ == "__main__":
    main()
