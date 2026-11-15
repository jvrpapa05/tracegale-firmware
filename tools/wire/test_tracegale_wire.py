# SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
# SPDX-License-Identifier: Apache-2.0
import json
import random
import unittest
from pathlib import Path

import tracegale_wire as wire

VECTORS = Path(__file__).parents[2] / "docs" / "protocol" / "vectors.json"


class Vectors(unittest.TestCase):
    """The vectors are shared with the firmware, so any implementation can be checked against the same bytes."""

    def test_valid_round_trip(self):
        for vector in json.loads(VECTORS.read_text())["valid"]:
            with self.subTest(vector["name"]):
                raw = bytes.fromhex(vector["hex"])
                described = wire.describe(raw)
                self.assertEqual(described["type"], vector["type"])
                _, kind, ident, body = wire.decode(raw)
                self.assertEqual(wire.encode(kind, ident, body), raw)

    def test_invalid_rejected(self):
        for vector in json.loads(VECTORS.read_text())["invalid"]:
            with self.subTest(vector["name"]):
                with self.assertRaises(wire.WireError):
                    wire.decode(bytes.fromhex(vector["hex"]))


class Limits(unittest.TestCase):
    def test_too_large(self):
        with self.assertRaises(wire.WireError):
            wire.decode(wire.encode(11, 1, {2: b"x" * 70000}))

    def test_huge_claimed_length_is_cheap(self):
        with self.assertRaises(wire.WireError):
            wire.decode(bytes.fromhex("5bffffffffffffffff"))

    def test_deep_nesting_stops(self):
        with self.assertRaises(wire.WireError):
            wire.decode(b"\x81" * 5000)

    def test_unknown_fields_and_types_stay_visible(self):
        described = wire.describe(wire.encode(99, 7, {0: 1, 40: b"\x01"}))
        self.assertEqual(described["type"], "unknown(99)")
        self.assertEqual(described["fields"]["40"], "01")

    def test_codes_are_named(self):
        self.assertEqual(wire.describe(wire.encode(3, 7, {0: 9}))["fields"]["code"], "9 (unknown_image)")
        self.assertEqual(wire.describe(wire.encode(24, 7, {0: 5, 1: 3}))["fields"]["outcome"], "3 (reverted)")
        self.assertEqual(wire.describe(wire.encode(24, 7, {0: 5, 1: 77}))["fields"]["outcome"], "77 (unknown)")

    def test_metrics_are_named(self):
        fields = wire.describe(wire.encode(4, 7, {0: 60, 1: {2: 5120, 77: 1}, 2: ["battery_mv", -3], 3: ["odd"]}))["fields"]
        self.assertEqual(fields["system"], {"heap_free": 5120, "77": 1})
        self.assertEqual(fields["gauges"], {"battery_mv": -3})
        self.assertEqual(fields["counters"], ["odd"], "an array that isn't pairs is shown as it is")

    def test_logs_are_named(self):
        fields = wire.describe(wire.encode(5, 7, {0: 9, 1: 5000, 2: [[0, 100, 2, "net", "Link down"], ["odd"]]}))["fields"]
        self.assertEqual(fields["records"][0], {"seq": 0, "uptime_ms": 100, "level": "2 (warning)", "module": "net", "text": "Link down"})
        self.assertEqual(fields["records"][1], ["odd"], "a record of the wrong shape is shown as it is")

    def test_future_version_rejected(self):
        with self.assertRaises(wire.WireError):
            wire.decode(wire.encode_item([2, 1, 1, {}]))

    def test_any_mangled_input_raises_only_wire_error(self):
        # A small fuzz, so a crash in the decoder shows up in the ordinary tests. Plan 16 adds real fuzzing.
        rng = random.Random(1)
        seed = wire.encode(11, 5, {0: 1, 1: 512, 2: bytes(32)})
        for _ in range(20000):
            raw = bytearray(seed if rng.random() < 0.7 else rng.randbytes(rng.randrange(40)))
            for _ in range(rng.randrange(4)):
                if raw:
                    raw[rng.randrange(len(raw))] = rng.randrange(256)
            try:
                wire.decode(bytes(raw))
            except wire.WireError:
                pass


if __name__ == "__main__":
    unittest.main()
