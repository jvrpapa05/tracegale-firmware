/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include "sha256.h"
#include "identity.h"
#include "wire.h"

struct vector {
	const char *name;
	const uint8_t *data;
	size_t len;
};

#include "vectors.inc"

ZTEST(wire, test_valid_vectors_accepted)
{
	for (size_t i = 0; i < ARRAY_SIZE(valid); i++) {
		struct tg_message msg;

		zassert_ok(tg_read_message(valid[i].data, valid[i].len, &msg), "%s", valid[i].name);
	}
}

ZTEST(wire, test_invalid_vectors_rejected)
{
	for (size_t i = 0; i < ARRAY_SIZE(invalid); i++) {
		struct tg_message msg;

		zassert_not_equal(tg_read_message(invalid[i].data, invalid[i].len, &msg), 0, "%s", invalid[i].name);
	}
}

ZTEST(wire, test_written_message_reads_back)
{
	static const uint8_t blob[] = {1, 2, 3, 4};
	uint8_t buf[64];
	struct tg_writer w;
	struct tg_message msg;
	uint32_t number;
	const uint8_t *data;
	size_t len;

	tg_write_begin(&w, buf, sizeof(buf), TG_HELLO, 70000, 3);
	tg_write_uint(&w, 0, 300);
	tg_write_bytes(&w, 1, blob, sizeof(blob));
	tg_write_text(&w, 2, "nrf52840dk");
	zassert_false(w.overflow);
	zassert_ok(tg_read_message(buf, w.len, &msg));
	zassert_equal(msg.type, TG_HELLO);
	zassert_equal(msg.id, 70000);
	zassert_true(tg_read_uint(&msg, 0, &number));
	zassert_equal(number, 300);
	zassert_true(tg_read_bytes(&msg, 1, &data, &len));
	zassert_equal(len, sizeof(blob));
	zassert_mem_equal(data, blob, sizeof(blob));
	/* The wrong type, and a missing key. */
	zassert_false(tg_read_uint(&msg, 1, &number));
	zassert_false(tg_read_uint(&msg, 9, &number));
}

ZTEST(wire, test_writer_reports_overflow)
{
	uint8_t buf[8];
	struct tg_writer w;

	tg_write_begin(&w, buf, sizeof(buf), TG_HELLO, 1, 1);
	tg_write_text(&w, 0, "too long for this buffer");
	zassert_true(w.overflow);
}

ZTEST(wire, test_different_version_is_told_apart)
{
	/* [2, 1, 1, {}] */
	static const uint8_t msg_v2[] = {0x84, 0x02, 0x01, 0x01, 0xa0};
	struct tg_message msg;

	zassert_equal(tg_read_message(msg_v2, sizeof(msg_v2), &msg), -ENOTSUP);
}

ZTEST(wire, test_mangled_input_is_only_rejected)
{
	static const uint8_t seed[] = {0x84, 0x01, 0x0b, 0x03, 0xa3, 0x00, 0x07, 0x01, 0x00, 0x02, 0x44, 1, 2, 3, 4};
	uint8_t buf[sizeof(seed)];

	/* The fuzz tests in tests/fuzz go much further. This keeps a crash in the parser from reaching main. */
	for (int i = 0; i < 20000; i++) {
		struct tg_message msg;
		uint32_t number;
		const uint8_t *data;
		size_t len;
		bool flag;

		memcpy(buf, seed, sizeof(buf));
		for (uint32_t k = sys_rand32_get() % 4; k > 0; k--) {
			buf[sys_rand32_get() % sizeof(buf)] = sys_rand32_get();
		}
		if (tg_read_message(buf, sys_rand32_get() % (sizeof(buf) + 1), &msg) == 0) {
			(void)tg_read_uint(&msg, 1, &number);
			(void)tg_read_bytes(&msg, 2, &data, &len);
			(void)tg_read_bool(&msg, 3, &flag);
		}
	}
}

ZTEST(wire, test_bool_round_trip)
{
	uint8_t buf[32];
	struct tg_writer w;
	struct tg_message msg;
	bool value = false;
	uint32_t number;

	tg_write_begin(&w, buf, sizeof(buf), TG_XFER_STATUS, 5, 2);
	tg_write_bool(&w, 0, true);
	tg_write_bool(&w, 1, false);
	zassert_false(w.overflow);
	zassert_ok(tg_read_message(buf, w.len, &msg));
	zassert_true(tg_read_bool(&msg, 0, &value) && value);
	zassert_true(tg_read_bool(&msg, 1, &value) && !value);
	zassert_false(tg_read_uint(&msg, 0, &number), "a bool isn't a number");
	zassert_false(tg_read_bool(&msg, 2, &value), "no such field");
}

ZTEST(wire, test_reserved_bytes)
{
	uint8_t buf[16];
	struct tg_writer w;
	struct tg_message msg;
	const uint8_t *data;
	size_t len;
	uint8_t *space;

	tg_write_begin(&w, buf, sizeof(buf), TG_XFER_CHUNK, 1, 1);
	space = tg_write_bytes_reserve(&w, 2, 4);
	zassert_not_null(space);
	memcpy(space, "abcd", 4);
	zassert_ok(tg_read_message(buf, w.len, &msg));
	zassert_true(tg_read_bytes(&msg, 2, &data, &len));
	zassert_equal(len, 4);
	zassert_mem_equal(data, "abcd", 4);

	tg_write_begin(&w, buf, sizeof(buf), TG_XFER_CHUNK, 1, 1);
	zassert_is_null(tg_write_bytes_reserve(&w, 2, sizeof(buf)), "too large for the buffer");
	zassert_true(w.overflow);
}

/* FIPS 180-4 examples, and the long message from the NIST test vectors. */
static void check_sha(const char *text, size_t repeat, const char *expected_hex)
{
	struct tg_sha256 ctx;
	uint8_t digest[TG_SHA256_SIZE];
	uint8_t expected[TG_SHA256_SIZE];

	zassert_equal(hex2bin(expected_hex, strlen(expected_hex), expected, sizeof(expected)), sizeof(expected));
	tg_sha256_init(&ctx);
	for (size_t i = 0; i < repeat; i++) {
		tg_sha256_update(&ctx, (const uint8_t *)text, strlen(text));
	}
	tg_sha256_final(&ctx, digest);
	zassert_mem_equal(digest, expected, sizeof(digest), "SHA-256 of \"%s\" x %u", text, (unsigned int)repeat);
}

ZTEST(wire, test_sha256_vectors)
{
	check_sha("", 1, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	check_sha("abc", 1, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	check_sha("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 1,
		  "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
	check_sha("a", 1000000, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
	/* Lengths either side of the padding boundaries, checked against Python's hashlib. */
	check_sha("0123456789012345678901234567890123456789012345678901234", 1,
		  "f34d5a0f80c0cbf84c8c0b90218c22637abd199965249da736a20143c8c9c9d9");
	check_sha("01234567890123456789012345678901234567890123456789012345", 1,
		  "83aa034bda83e458a0dc9cbce0d4e354716aa0ff770ed37ac0ed2b292052e4af");
	check_sha("012345678901234567890123456789012345678901234567890123456789012", 1,
		  "074f6e9ac301d5d1b6df6f1dfb8c6f89c187ea945d352ce6a29279a9c630680b");
	check_sha("0123456789012345678901234567890123456789012345678901234567890123", 1,
		  "9674d9e078535b7cec43284387a6ee39956188e735a85452b0050b55341cda56");
	check_sha("01234567890123456789012345678901234567890123456789012345678901234", 1,
		  "52774b57c10e45040a61c14d35c1c8ebefe880082313aa0a21ebb077734cd067");
}

/* Uptimes in milliseconds pass 32 bits after 49 days. */
ZTEST(wire, test_values_beyond_32_bits)
{
	static const uint8_t large[] = {0x01, 0x1b, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x05};
	uint8_t buf[32], small[32];
	struct tg_writer w, s;

	tg_write_begin(&w, buf, sizeof(buf), TG_LOGS, 1, 1);
	tg_write_uint64(&w, 1, (1ULL << 40) + 5);
	zassert_false(w.overflow);
	zassert_mem_equal(&buf[w.len - sizeof(large)], large, sizeof(large));
	zassert_equal(tg_uint64_size(1ULL << 40), 9);

	/* Up to 32 bits, the same shortest form as before. */
	tg_write_begin(&w, buf, sizeof(buf), TG_LOGS, 1, 1);
	tg_write_uint64(&w, 1, 70000);
	tg_write_begin(&s, small, sizeof(small), TG_LOGS, 1, 1);
	tg_write_uint(&s, 1, 70000);
	zassert_equal(w.len, s.len);
	zassert_mem_equal(buf, small, s.len);
	zassert_equal(tg_uint64_size(70000), tg_uint_size(70000));
}

/* A device certificate as Tracegale's server issues them: the issuer has a common name too, which mustn't be taken. Made with
 * openssl for this test; its key was thrown away.
 */
static const uint8_t device_cert[] = {
	0x30, 0x82, 0x01, 0x6e, 0x30, 0x82, 0x01, 0x14, 0x02, 0x14, 0x6e, 0x77, 0x1e, 0xeb, 0x81, 0xd8,
	0x09, 0x3a, 0xd0, 0x40, 0x8b, 0x7c, 0xb6, 0x4e, 0xa9, 0xbe, 0x9e, 0x82, 0x70, 0xc2, 0x30, 0x0a,
	0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x02, 0x30, 0x30, 0x31, 0x1c, 0x30, 0x1a,
	0x06, 0x03, 0x55, 0x04, 0x03, 0x0c, 0x13, 0x54, 0x72, 0x61, 0x63, 0x65, 0x67, 0x61, 0x6c, 0x65,
	0x20, 0x64, 0x65, 0x76, 0x69, 0x63, 0x65, 0x20, 0x43, 0x41, 0x31, 0x10, 0x30, 0x0e, 0x06, 0x03,
	0x55, 0x04, 0x0a, 0x0c, 0x07, 0x45, 0x78, 0x61, 0x6d, 0x70, 0x6c, 0x65, 0x30, 0x1e, 0x17, 0x0d,
	0x32, 0x36, 0x31, 0x31, 0x31, 0x35, 0x30, 0x30, 0x31, 0x36, 0x31, 0x38, 0x5a, 0x17, 0x0d, 0x33,
	0x36, 0x31, 0x31, 0x31, 0x32, 0x30, 0x30, 0x31, 0x36, 0x31, 0x38, 0x5a, 0x30, 0x43, 0x31, 0x12,
	0x30, 0x10, 0x06, 0x03, 0x55, 0x04, 0x0a, 0x0c, 0x09, 0x54, 0x72, 0x61, 0x63, 0x65, 0x67, 0x61,
	0x6c, 0x65, 0x31, 0x2d, 0x30, 0x2b, 0x06, 0x03, 0x55, 0x04, 0x03, 0x0c, 0x24, 0x64, 0x65, 0x76,
	0x2d, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x61, 0x62, 0x63, 0x64, 0x65,
	0x66, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x61, 0x62, 0x63, 0x64, 0x65,
	0x66, 0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01, 0x06, 0x08,
	0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00, 0x04, 0xfc, 0x4d, 0xbc, 0x27,
	0xcd, 0xc4, 0x55, 0x8f, 0xd6, 0x50, 0x15, 0x01, 0x11, 0xee, 0x2a, 0x33, 0x75, 0x46, 0x52, 0x5c,
	0x23, 0x7f, 0x07, 0xc4, 0xc8, 0x41, 0xdb, 0x7f, 0x80, 0x8c, 0x9d, 0xe7, 0xd6, 0xf2, 0xf3, 0x52,
	0x89, 0x15, 0xd5, 0x19, 0xeb, 0x62, 0xaa, 0x5d, 0xcb, 0x3b, 0xbe, 0xe8, 0xb2, 0x50, 0x9b, 0xf5,
	0x62, 0x24, 0x72, 0x49, 0xac, 0x23, 0xff, 0x4c, 0x6f, 0x4d, 0xe6, 0xfa, 0x30, 0x0a, 0x06, 0x08,
	0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x02, 0x03, 0x48, 0x00, 0x30, 0x45, 0x02, 0x20, 0x70,
	0xc9, 0xf2, 0xcc, 0xce, 0x98, 0x73, 0x39, 0x96, 0x41, 0x9b, 0xc6, 0xa2, 0xc7, 0xbf, 0x46, 0xfc,
	0x81, 0x8c, 0xc9, 0xaa, 0xe5, 0xf6, 0x5c, 0x9f, 0xcf, 0x89, 0x95, 0x94, 0x0a, 0xdc, 0x0b, 0x02,
	0x21, 0x00, 0xeb, 0x1e, 0x7b, 0x96, 0xbd, 0xe8, 0x88, 0xe9, 0x9a, 0x28, 0xea, 0x2c, 0x94, 0x67,
	0x38, 0xba, 0xe4, 0x6c, 0x43, 0xef, 0xa2, 0xf0, 0xdd, 0x45, 0x13, 0x8c, 0xdd, 0x1f, 0xe7, 0xcb,
	0x9d, 0x56,
};

ZTEST(wire, test_identity_from_a_certificate)
{
	char id[TG_IDENTITY_MAX + 1];
	uint8_t copy[sizeof(device_cert)];

	zassert_ok(tg_identity_from_cert(device_cert, sizeof(device_cert), id, sizeof(id)));
	zassert_str_equal(id, "dev-0123456789abcdef0123456789abcdef");

	/* Too small a buffer, nothing at all, and every cut-short copy are refused, never read past their end. */
	zassert_equal(tg_identity_from_cert(device_cert, sizeof(device_cert), id, 10), -1);
	zassert_equal(tg_identity_from_cert(NULL, 0, id, sizeof(id)), -1);
	for (size_t len = 0; len < sizeof(device_cert) - 64; len++) {
		zassert_equal(tg_identity_from_cert(device_cert, len, id, sizeof(id)), -1, "cut at %zu", len);
	}
	/* A name with a character a topic can't hold is refused. */
	memcpy(copy, device_cert, sizeof(copy));
	for (size_t i = 0; i + 4 < sizeof(copy); i++) {
		if (memcmp(&copy[i], "dev-", 4) == 0) {
			copy[i + 3] = '/';
		}
	}
	zassert_equal(tg_identity_from_cert(copy, sizeof(copy), id, sizeof(id)), -1);
}

ZTEST_SUITE(wire, NULL, NULL, NULL, NULL, NULL);
