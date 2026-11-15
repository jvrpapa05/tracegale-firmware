#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
# SPDX-License-Identifier: Apache-2.0
"""Compares what Tracegale adds to a firmware image against a stored baseline.

Any increase beyond a few bytes of noise fails, so that growth is seen in review. To accept growth, re-run
with --update and commit the changed baseline.

Exit codes: 0 within baseline, 1 over baseline, 2 missing baseline entry or bad input.
"""

import argparse
import json
import os
import sys
from dataclasses import dataclass
from pathlib import Path

SHF_WRITE = 0x1
SHF_ALLOC = 0x2
SHT_NOBITS = 8


@dataclass(frozen=True)
class Size:
    rom: int
    ram: int


def size_from_sections(sections):
    """Sums (name, type, flags, size, copied) tuples into bytes stored in the image and bytes used in RAM.

    `copied` is true for a section whose load address differs from its run address: data that the startup code copies from
    flash into RAM. A writable section uses RAM if it's zeroed at start (NOBITS), or copied. Where some section is copied, the
    image runs from flash, and a writable section that isn't copied stays in flash: the nRF Connect SDK's nRF91 builds mark
    rodata writable, which would otherwise count it as RAM. An image that copies nothing runs from RAM, so all of it counts.

    Padding sections are skipped. They hold alignment filler that shrinks as code grows, such as
    Zephyr's rom_mpu_padding, which would otherwise look like a drop in RAM use.
    """
    sections = [section for section in sections if section[2] & SHF_ALLOC and not section[0].endswith("_padding")]
    runs_from_flash = any(copied for _, _, _, _, copied in sections)
    rom = ram = 0
    for _, section_type, flags, size, copied in sections:
        if section_type != SHT_NOBITS:
            rom += size
        if flags & SHF_WRITE and (section_type == SHT_NOBITS or copied or not runs_from_flash):
            ram += size
    return Size(rom, ram)


def load_address(section, segments):
    """The address a section is stored at: its run address moved by its segment's physical address, if any segment holds it."""
    for vaddr, paddr, memsz in segments:
        if vaddr <= section["sh_addr"] < vaddr + memsz:
            return section["sh_addr"] - vaddr + paddr
    return section["sh_addr"]


def size_of_elf(path):
    from elftools.elf.constants import SH_FLAGS  # pylint: disable=import-outside-toplevel
    from elftools.elf.elffile import ELFFile  # pylint: disable=import-outside-toplevel

    assert SH_FLAGS.SHF_ALLOC == SHF_ALLOC and SH_FLAGS.SHF_WRITE == SHF_WRITE
    with open(path, "rb") as stream:
        elf = ELFFile(stream)
        segments = [
            (segment["p_vaddr"], segment["p_paddr"], segment["p_memsz"])
            for segment in elf.iter_segments()
            if segment["p_type"] == "PT_LOAD"
        ]
        sections = [
            (
                section.name,
                SHT_NOBITS if section["sh_type"] == "SHT_NOBITS" else 0,
                section["sh_flags"],
                section["sh_size"],
                section["sh_type"] != "SHT_NOBITS" and load_address(section, segments) != section["sh_addr"],
            )
            for section in elf.iter_sections()
        ]
    return size_from_sections(sections)


# The same source measures a few bytes apart on different machines: the build's paths end up in the image, and their length
# shifts alignment. GitHub's runners measured logs at 4 bytes more than this repository's baseline from the development host.
NOISE_BYTES = 32


def compare(delta, allowed):
    """Returns a list of messages, one for each measure over its allowed value plus the noise allowance."""
    return [
        f"{name} grew: {current} bytes, baseline {limit} bytes, more than {NOISE_BYTES} bytes over"
        for name, current, limit in (
            ("ROM", delta.rom, allowed["rom"]),
            ("RAM", delta.ram, allowed["ram"]),
        )
        if current > limit + NOISE_BYTES
    ]


def load_baseline(path):
    if not path.exists():
        return {}
    return json.loads(path.read_text(encoding="utf-8"))


def save_baseline(path, baseline):
    path.write_text(json.dumps(baseline, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def report(key, delta, allowed):
    limit = f"{allowed['rom']} / {allowed['ram']}" if allowed else "none"
    line = f"| {key} | {delta.rom} | {delta.ram} | {limit} |"
    print(f"Tracegale size for {key}: ROM {delta.rom} bytes, RAM {delta.ram} bytes (baseline: {limit})")
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as stream:
            stream.write(line + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--key", required=True, help="Zephyr tag and board, such as v4.5.0/qemu_riscv32")
    parser.add_argument("--on", type=Path, required=True, help="ELF built with Tracegale on")
    parser.add_argument("--off", type=Path, required=True, help="ELF built with Tracegale off")
    parser.add_argument("--update", action="store_true", help="Store this result as the baseline")
    args = parser.parse_args()

    on, off = size_of_elf(args.on), size_of_elf(args.off)
    delta = Size(on.rom - off.rom, on.ram - off.ram)
    baseline = load_baseline(args.baseline)

    if args.update:
        baseline[args.key] = {"rom": delta.rom, "ram": delta.ram}
        save_baseline(args.baseline, baseline)

    allowed = baseline.get(args.key)
    report(args.key, delta, allowed)

    if allowed is None:
        print(f"No baseline for {args.key}. Run firmware-check.sh with --update-size-baseline.", file=sys.stderr)
        return 2

    problems = compare(delta, allowed)
    for problem in problems:
        print(
            f"{args.key}: {problem}. If this growth is intended, update the baseline and commit it.",
            file=sys.stderr,
        )
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
