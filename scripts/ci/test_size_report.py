# SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
# SPDX-License-Identifier: Apache-2.0

import unittest

from size_report import NOISE_BYTES, SHF_ALLOC, SHF_WRITE, SHT_NOBITS, Size, compare, size_from_sections

PROGBITS = 1


class SizeFromSectionsTest(unittest.TestCase):
    # An image that runs from flash: initialised data is copied into RAM at start.
    data = ("datas", PROGBITS, SHF_ALLOC | SHF_WRITE, 10, True)

    def test_code_counts_only_as_rom(self):
        self.assertEqual(size_from_sections([("text", PROGBITS, SHF_ALLOC, 100, False), self.data]), Size(110, 10))

    def test_initialised_data_counts_as_rom_and_ram(self):
        self.assertEqual(size_from_sections([self.data]), Size(10, 10))

    def test_zeroed_data_counts_only_as_ram(self):
        self.assertEqual(size_from_sections([("bss", SHT_NOBITS, SHF_ALLOC | SHF_WRITE, 20, False)]), Size(0, 20))

    def test_debug_sections_are_ignored(self):
        self.assertEqual(size_from_sections([("debug_info", PROGBITS, 0, 5000, False)]), Size(0, 0))

    def test_padding_is_ignored(self):
        section = ("rom_mpu_padding", SHT_NOBITS, SHF_ALLOC | SHF_WRITE, 2988, False)
        self.assertEqual(size_from_sections([section]), Size(0, 0))

    def test_writable_section_left_in_flash_counts_only_as_rom(self):
        # The nRF Connect SDK's nRF91 builds mark rodata writable, though it stays in flash.
        rodata = ("rodata", PROGBITS, SHF_ALLOC | SHF_WRITE, 300, False)
        self.assertEqual(size_from_sections([rodata, self.data]), Size(310, 10))

    def test_image_running_from_ram_counts_all_writable_sections(self):
        data = ("datas", PROGBITS, SHF_ALLOC | SHF_WRITE, 10, False)
        self.assertEqual(size_from_sections([("text", PROGBITS, SHF_ALLOC, 100, False), data]), Size(110, 10))


class CompareTest(unittest.TestCase):
    allowed = {"rom": 100, "ram": 50}

    def test_equal_passes(self):
        self.assertEqual(compare(Size(100, 50), self.allowed), [])

    def test_smaller_passes(self):
        self.assertEqual(compare(Size(90, 40), self.allowed), [])

    def test_noise_passes(self):
        self.assertEqual(compare(Size(100 + NOISE_BYTES, 50 + NOISE_BYTES), self.allowed), [])

    def test_rom_growth_fails(self):
        self.assertEqual(len(compare(Size(101 + NOISE_BYTES, 50), self.allowed)), 1)

    def test_ram_growth_fails(self):
        self.assertEqual(len(compare(Size(100, 51 + NOISE_BYTES), self.allowed)), 1)

    def test_both_grow_reports_both(self):
        self.assertEqual(len(compare(Size(101 + NOISE_BYTES, 51 + NOISE_BYTES), self.allowed)), 2)


if __name__ == "__main__":
    unittest.main()
