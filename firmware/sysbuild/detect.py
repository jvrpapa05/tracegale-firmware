#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
# SPDX-License-Identifier: Apache-2.0
"""Answers sysbuild Kconfig questions that sysbuild can't answer by itself.

Sysbuild's Kconfig runs before any image is configured, so it can't see the application's Kconfig
or devicetree. This script reads what it needs from the source files and prints y or n:

  app-enabled       Does the application's configuration set CONFIG_TRACEGALE=y?
  boot-partition    Does the board's devicetree, or an application overlay, define boot_partition?
  mcuboot-usable    Is the MCUboot module present, with a boot_partition, on a board not known to fail?

It reads its inputs from the environment set by Kconfig and by modules/modules.cmake. Any failure
prints n, so that a problem here leaves a build without OTA rather than breaking it. Stderr carries the
reason, and the build warns that OTA is off.
"""

import os
import re
import sys
from pathlib import Path

CONFIG_LINE = re.compile(r"^\s*CONFIG_TRACEGALE\s*=\s*(\S+)")
INCLUDE_LINE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)
BOOT_PARTITION = re.compile(r"\bboot_partition\s*:")
POSIX_ARCH = re.compile(r"^\s*arch:\s*posix\s*$", re.MULTILINE)


def split_paths(value):
    """Splits a CMake list or space-separated list of paths."""
    return [path for path in re.split(r"[;\s]+", value or "") if path]


def board_file_names():
    """Returns the base names Zephyr tries for a board, most specific first.

    For BOARD=nrf52840dk and BOARD_QUALIFIERS=/nrf52840, this starts with nrf52840dk_nrf52840. Boards
    with one SoC usually leave it out of the file name, so shorter forms are tried as well.
    """
    board = os.environ.get("BOARD", "")
    qualifiers = [part for part in os.environ.get("BOARD_QUALIFIERS", "").split("/") if part]
    if not board:
        return []
    names = []
    for count in range(len(qualifiers), -1, -1):
        names.append("_".join([board, *qualifiers[:count]]))
    if len(qualifiers) > 1:
        names.insert(1, "_".join([board, *qualifiers[1:]]))
    return names


def module_dirs():
    return [
        Path(value)
        for key, value in os.environ.items()
        if key.startswith("ZEPHYR_") and key.endswith("_MODULE_DIR") and value
    ]


def app_dir():
    value = os.environ.get("TRACEGALE_APP_DIR")
    return Path(value) if value else None


def app_enabled():
    """Applies the application's configuration files in Zephyr's order. The last value wins."""
    application = app_dir()
    if application is None:
        return False

    files = []
    conf_file = split_paths(os.environ.get("TRACEGALE_CONF_FILE"))
    if conf_file:
        files.extend(conf_file)
    else:
        files.append("prj.conf")
        files.extend(f"boards/{name}.conf" for name in reversed(board_file_names()))
    files.extend(split_paths(os.environ.get("TRACEGALE_EXTRA_CONF_FILE")))

    enabled = False
    for name in files:
        path = Path(name) if Path(name).is_absolute() else application / name
        if not path.is_file():
            continue
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            match = CONFIG_LINE.match(line)
            if match:
                enabled = match.group(1) == "y"

    override = os.environ.get("TRACEGALE_CONFIG_OVERRIDE")
    if override:
        enabled = override.lower() in ("y", "1", "on", "true", "yes")
    return enabled


def dts_roots():
    """Returns the folders Zephyr searches for devicetree includes."""
    roots = []
    base = Path(os.environ.get("srctree") or os.environ.get("ZEPHYR_BASE", ""))
    for root in [base, *module_dirs()]:
        for candidate in (root / "dts", root / "include"):
            if candidate.is_dir():
                roots.append(candidate)
                roots.extend(path for path in candidate.iterdir() if path.is_dir())
    return roots


def contains_boot_partition(start, roots, seen):
    """Follows includes from a devicetree file, looking for a boot_partition label.

    This reads text and doesn't evaluate #if blocks, so a label inside a disabled block counts as found.
    That case fails the MCUboot build with a devicetree error, and SB_CONFIG_BOOTLOADER_NONE=y overrides it.
    """
    try:
        path = start.resolve()
    except OSError:
        return False
    if path in seen or not path.is_file():
        return False
    seen.add(path)

    text = path.read_text(encoding="utf-8", errors="replace")
    if BOOT_PARTITION.search(text):
        return True
    for include in INCLUDE_LINE.findall(text):
        for root in [path.parent, *roots]:
            candidate = root / include
            if candidate.is_file():
                if contains_boot_partition(candidate, roots, seen):
                    return True
                break
    return False


def find_board_dts():
    """Finds the board's .dts file in Zephyr and in each module's boards folder."""
    base = Path(os.environ.get("srctree") or os.environ.get("ZEPHYR_BASE", ""))
    search = [root / "boards" for root in [base, *module_dirs()]]
    application = app_dir()
    if application is not None:
        search.append(application / "boards")
    for name in board_file_names():
        for root in search:
            if root.is_dir():
                for path in root.rglob(f"{name}.dts"):
                    return path
    return None


def is_host_simulator(board_folder):
    """Checks the board's test metadata, where Zephyr marks host-based boards as "arch: posix"."""
    for path in board_folder.iterdir():
        if path.suffix in (".yaml", ".yml") and path.is_file():
            if POSIX_ARCH.search(path.read_text(encoding="utf-8", errors="replace")):
                return True
    return False


def boot_partition():
    application = app_dir()
    roots = dts_roots()
    seen = set()

    if application is not None:
        overlays = [application / "app.overlay"]
        overlays.extend(application / "boards" / f"{name}.overlay" for name in board_file_names())
        overlays.extend(Path(path) for path in split_paths(os.environ.get("TRACEGALE_DTC_OVERLAY")))
        for overlay in overlays:
            if contains_boot_partition(overlay, roots, seen):
                return True

    board_dts = find_board_dts()
    if board_dts is None:
        print(f"Tracegale: no devicetree found for board {os.environ.get('BOARD')}", file=sys.stderr)
        return False
    # Simulated boards can define boot_partition, but they run as host programs that MCUboot can't boot.
    if is_host_simulator(board_dts.parent):
        return False
    return contains_boot_partition(board_dts, roots, seen)


def known_unsupported():
    """Reads the boards where MCUboot is known not to build, one board target per line."""
    path = Path(__file__).with_name("mcuboot-unsupported-boards.txt")
    # Real builds pass qualifiers without a leading slash, such as "nrf5340/cpuapp".
    qualifiers = os.environ.get("BOARD_QUALIFIERS", "").strip("/")
    board = os.environ.get("BOARD", "")
    if qualifiers:
        board = f"{board}/{qualifiers}"
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.split("#")[0].strip() == board:
            return True
    return False


def mcuboot_usable():
    """Tells whether turning on MCUboot should work for this workspace, board and application."""
    if not os.environ.get("ZEPHYR_MCUBOOT_MODULE_DIR"):
        return False
    return not known_unsupported() and boot_partition()


def main():
    questions = {
        "app-enabled": app_enabled,
        "boot-partition": boot_partition,
        "mcuboot-usable": mcuboot_usable,
    }
    if len(sys.argv) != 2 or sys.argv[1] not in questions:
        print("n")
        return 0
    try:
        print("y" if questions[sys.argv[1]]() else "n")
    except OSError as error:
        print(f"Tracegale: {error}", file=sys.stderr)
        print("n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
