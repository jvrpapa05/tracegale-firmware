# SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
# SPDX-License-Identifier: Apache-2.0

import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import detect


class DetectTestCase(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.zephyr = self.root / "zephyr"
        self.app = self.root / "app"
        self.app.mkdir()
        (self.zephyr / "dts" / "arm").mkdir(parents=True)
        (self.zephyr / "boards" / "acme" / "board_a").mkdir(parents=True)

        patcher = mock.patch.dict(
            os.environ,
            {
                "srctree": str(self.zephyr),
                "BOARD": "board_a",
                "BOARD_QUALIFIERS": "/soc_a",
                "TRACEGALE_APP_DIR": str(self.app),
            },
            clear=True,
        )
        patcher.start()
        self.addCleanup(patcher.stop)

    def write(self, path, text):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    def board_file(self, name="board_a_soc_a.dts"):
        return self.zephyr / "boards" / "acme" / "board_a" / name


class AppEnabledTest(DetectTestCase):
    def test_line_enables(self):
        self.write(self.app / "prj.conf", "CONFIG_TRACEGALE=y\n")
        self.assertTrue(detect.app_enabled())

    def test_missing_line_stays_off(self):
        self.write(self.app / "prj.conf", "CONFIG_LOG=y\n")
        self.assertFalse(detect.app_enabled())

    def test_missing_file_stays_off(self):
        self.assertFalse(detect.app_enabled())

    def test_comment_stays_off(self):
        self.write(self.app / "prj.conf", "# CONFIG_TRACEGALE=y\n")
        self.assertFalse(detect.app_enabled())

    def test_similar_name_stays_off(self):
        self.write(self.app / "prj.conf", "CONFIG_TRACEGALE_LOGS=y\n")
        self.assertFalse(detect.app_enabled())

    def test_last_value_wins_within_a_file(self):
        self.write(self.app / "prj.conf", "CONFIG_TRACEGALE=y\nCONFIG_TRACEGALE=n\n")
        self.assertFalse(detect.app_enabled())

    def test_board_file_overrides_prj_conf(self):
        self.write(self.app / "prj.conf", "CONFIG_TRACEGALE=y\n")
        self.write(self.app / "boards" / "board_a_soc_a.conf", "CONFIG_TRACEGALE=n\n")
        self.assertFalse(detect.app_enabled())

    def test_extra_conf_file_enables(self):
        self.write(self.app / "extra.conf", "CONFIG_TRACEGALE=y\n")
        os.environ["TRACEGALE_EXTRA_CONF_FILE"] = "extra.conf"
        self.assertTrue(detect.app_enabled())

    def test_conf_file_replaces_prj_conf(self):
        self.write(self.app / "prj.conf", "CONFIG_TRACEGALE=y\n")
        self.write(self.app / "other.conf", "CONFIG_LOG=y\n")
        os.environ["TRACEGALE_CONF_FILE"] = ";other.conf"
        self.assertFalse(detect.app_enabled())

    def test_command_line_overrides_files(self):
        self.write(self.app / "prj.conf", "CONFIG_TRACEGALE=y\n")
        os.environ["TRACEGALE_CONFIG_OVERRIDE"] = "n"
        self.assertFalse(detect.app_enabled())

    def test_no_application_stays_off(self):
        del os.environ["TRACEGALE_APP_DIR"]
        self.assertFalse(detect.app_enabled())


class BootPartitionTest(DetectTestCase):
    def test_label_in_board_file(self):
        self.write(self.board_file(), "boot_partition: partition@0 {};\n")
        self.assertTrue(detect.boot_partition())

    def test_no_label(self):
        self.write(self.board_file(), "/ { };\n")
        self.assertFalse(detect.boot_partition())

    def test_label_in_included_file(self):
        self.write(self.board_file(), '#include <arm/soc.dtsi>\n')
        self.write(self.zephyr / "dts" / "arm" / "soc.dtsi", "boot_partition: partition@0 {};\n")
        self.assertTrue(detect.boot_partition())

    def test_reference_is_not_a_label(self):
        self.write(self.board_file(), "zephyr,boot = &boot_partition;\n")
        self.assertFalse(detect.boot_partition())

    def test_include_cycle_ends(self):
        self.write(self.board_file(), '#include "a.dtsi"\n')
        self.write(self.board_file("a.dtsi"), '#include "board_a_soc_a.dts"\n')
        self.assertFalse(detect.boot_partition())

    def test_application_overlay_adds_label(self):
        self.write(self.board_file(), "/ { };\n")
        self.write(self.app / "app.overlay", "boot_partition: partition@0 {};\n")
        self.assertTrue(detect.boot_partition())

    def test_board_name_without_soc(self):
        self.write(self.board_file("board_a.dts"), "boot_partition: partition@0 {};\n")
        self.assertTrue(detect.boot_partition())

    def test_unknown_board(self):
        self.assertFalse(detect.boot_partition())

    def test_host_simulator_is_excluded(self):
        self.write(self.board_file(), "boot_partition: partition@0 {};\n")
        self.write(self.board_file("board_a_soc_a.yaml"), "arch: posix\n")
        self.assertFalse(detect.boot_partition())

    def test_real_architecture_is_kept(self):
        self.write(self.board_file(), "boot_partition: partition@0 {};\n")
        self.write(self.board_file("board_a_soc_a.yaml"), "arch: arm\n")
        self.assertTrue(detect.boot_partition())


class McubootUsableTest(DetectTestCase):
    def setUp(self):
        super().setUp()
        os.environ["ZEPHYR_MCUBOOT_MODULE_DIR"] = str(self.root / "mcuboot")
        self.write(self.board_file(), "boot_partition: partition@0 {};\n")

    def test_usable(self):
        self.assertTrue(detect.mcuboot_usable())

    def test_module_missing(self):
        del os.environ["ZEPHYR_MCUBOOT_MODULE_DIR"]
        self.assertFalse(detect.mcuboot_usable())

    def test_no_boot_partition(self):
        self.write(self.board_file(), "/ { };\n")
        self.assertFalse(detect.mcuboot_usable())

    def test_listed_board(self):
        os.environ["BOARD"] = "nrf52840dongle"
        os.environ["BOARD_QUALIFIERS"] = "/nrf52840"
        self.write(self.board_file("nrf52840dongle_nrf52840.dts"), "boot_partition: partition@0 {};\n")
        self.assertFalse(detect.mcuboot_usable())

    def test_listed_board_with_qualifiers_as_real_builds_pass_them(self):
        os.environ["BOARD"] = "nrf52840dongle"
        os.environ["BOARD_QUALIFIERS"] = "nrf52840"
        self.write(self.board_file("nrf52840dongle_nrf52840.dts"), "boot_partition: partition@0 {};\n")
        self.assertFalse(detect.mcuboot_usable())

    def test_listed_board_with_soc_and_cpu_as_real_builds_pass_them(self):
        os.environ["BOARD"] = "thingy53"
        os.environ["BOARD_QUALIFIERS"] = "nrf5340/cpuapp"
        self.write(self.board_file("thingy53_nrf5340_cpuapp.dts"), "boot_partition: partition@0 {};\n")
        self.assertFalse(detect.mcuboot_usable())

    def test_other_qualifier_of_a_listed_board_is_not_listed(self):
        os.environ["BOARD"] = "nrf52840dongle"
        os.environ["BOARD_QUALIFIERS"] = "/other"
        self.write(self.board_file("nrf52840dongle_other.dts"), "boot_partition: partition@0 {};\n")
        self.assertTrue(detect.mcuboot_usable())


class BoardFileNamesTest(DetectTestCase):
    def test_soc_and_cpu(self):
        os.environ["BOARD"] = "dk"
        os.environ["BOARD_QUALIFIERS"] = "/nrf5340/cpuapp"
        self.assertEqual(
            detect.board_file_names(),
            ["dk_nrf5340_cpuapp", "dk_cpuapp", "dk_nrf5340", "dk"],
        )

    def test_no_qualifiers(self):
        os.environ["BOARD"] = "dk"
        os.environ["BOARD_QUALIFIERS"] = ""
        self.assertEqual(detect.board_file_names(), ["dk"])


if __name__ == "__main__":
    unittest.main()
