/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * The crash store and upload, fed the way Zephyr's coredump code feeds a backend, and sent to a fake server that follows
 * "Device to server" in docs/protocol/README.md.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/debug/coredump.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include <tracegale/tracegale.h>

#include "build_id.h"
#include "crash.h"
#include "fake.h"
#include "sha256.h"
#include "transport.h"
#include "wire.h"

extern struct coredump_backend_api coredump_backend_other;

/* Builds a dump in Zephyr's format: a file header, then blocks. */
uint8_t dump[2048];
size_t dump_len;

void dump_begin(void)
{
	static const uint8_t header[12] = {'Z', 'E', 2, 0, 3, 0, 5, 0, 3, 0, 0, 0};

	memcpy(dump, header, sizeof(header));
	dump_len = sizeof(header);
}

void dump_block(char id, size_t len)
{
	dump[dump_len++] = id;
	sys_put_le16(2, &dump[dump_len]);
	sys_put_le16(len, &dump[dump_len + 2]);
	dump_len += 4;
	for (size_t i = 0; i < len; i++) {
		dump[dump_len++] = (uint8_t)(0xa0 + i);
	}
}

void dump_memory(uint32_t start, size_t len)
{
	dump[dump_len++] = 'M';
	sys_put_le16(1, &dump[dump_len]);
	sys_put_le32(start, &dump[dump_len + 2]);
	sys_put_le32(start + len, &dump[dump_len + 6]);
	dump_len += 10;
	for (size_t i = 0; i < len; i++) {
		dump[dump_len++] = (uint8_t)i;
	}
}

void feed(size_t piece)
{
	coredump_backend_other.start();
	for (size_t offset = 0; offset < dump_len; offset += piece) {
		coredump_backend_other.buffer_output(&dump[offset], MIN(piece, dump_len - offset));
	}
	coredump_backend_other.end();
}

size_t stored(uint8_t *out, size_t cap)
{
	int size = coredump_query(COREDUMP_QUERY_GET_STORED_DUMP_SIZE, NULL);
	struct coredump_cmd_copy_arg arg = {.offset = 0, .buffer = out, .length = MIN((size_t)size, cap)};

	if (size <= 0) {
		return 0;
	}
	zassert_equal(coredump_cmd(COREDUMP_CMD_COPY_STORED_DUMP, &arg), size);
	return size;
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	(void)coredump_cmd(COREDUMP_CMD_ERASE_STORED_DUMP, NULL);
#if defined(CONFIG_TRACEGALE_CRASH_KEEP)
	zassert_ok(tg_crash_kept_forget());
#endif
}

ZTEST(crash_store, test_small_dump_is_kept_whole)
{
	static uint8_t out[STORE_SIZE];
	uint8_t id[TG_BUILD_ID_MAX];
	const uint8_t *own;
	size_t own_len = tg_build_id(&own);

	zassert_equal(coredump_query(COREDUMP_QUERY_HAS_STORED_DUMP, NULL), 0);
	dump_begin();
	dump_block('A', 40);
	dump_memory(0x20001000, 100);
	feed(dump_len);
	zassert_equal(coredump_query(COREDUMP_QUERY_HAS_STORED_DUMP, NULL), 1);
	zassert_equal(coredump_cmd(COREDUMP_CMD_VERIFY_STORED_DUMP, NULL), 1);
	zassert_equal(stored(out, sizeof(out)), dump_len + MARK);
	zassert_mem_equal(out, dump, dump_len);
	zassert_equal(out[dump_len], 'M', "the mark is a memory block");
	zassert_equal(sys_get_le32(&out[dump_len + 7]) - sys_get_le32(&out[dump_len + 3]), 8);
	zassert_equal(tg_crash_stored_flags(), 0);
	zassert_equal(tg_crash_pending(), dump_len + MARK);

	zassert_true(own_len > 0, "the image has a build ID");
	zassert_equal(tg_crash_stored_build_id(id, sizeof(id)), own_len);
	zassert_mem_equal(id, own, own_len);
}

ZTEST(crash_store, test_identical_crashes_give_different_dumps)
{
	static uint8_t first[STORE_SIZE], second[STORE_SIZE];

	dump_begin();
	dump_block('A', 40);
	feed(dump_len);
	zassert_equal(stored(first, sizeof(first)), dump_len + MARK);
	feed(dump_len);
	zassert_equal(stored(second, sizeof(second)), dump_len + MARK);
	/* Otherwise the server would take the second for the first, resumed, and never count it. */
	zassert_true(memcmp(first, second, dump_len + MARK) != 0);
}

ZTEST(crash_store, test_pieces_of_any_size_give_the_same_dump)
{
	static uint8_t whole[STORE_SIZE], out[STORE_SIZE];
	size_t len;

	dump_begin();
	dump_block('A', 30);
	dump_block('T', 7);
	dump_memory(0x20000000, 64);
	dump_memory(0x20000400, 600);
	feed(dump_len);
	len = stored(whole, sizeof(whole));
	for (size_t piece = 1; piece < 20; piece++) {
		feed(piece);
		zassert_equal(stored(out, sizeof(out)), len, "piece size %u", (unsigned int)piece);
		/* Only the mark's contents differ. */
		zassert_mem_equal(out, whole, len - 8, "piece size %u", (unsigned int)piece);
	}
}

ZTEST(crash_store, test_large_memory_is_cut_at_its_end)
{
	static uint8_t out[STORE_SIZE];
	size_t len;
	size_t before_memory;

	dump_begin();
	dump_block('A', 40);
	before_memory = dump_len;
	dump_memory(0x20001000, 1000);
	/* Comes after a block that filled the store, so there's no room left for it. */
	dump_memory(0x20003000, 40);
	feed(7);
	len = stored(out, sizeof(out));
	zassert_true(len <= STORE_SIZE && len > STORE_SIZE - 14 - MARK, "used %u", (unsigned int)len);
	zassert_mem_equal(out, dump, before_memory + 6, "everything before the cut, and the start address, is unchanged");
	uint32_t start = sys_get_le32(&out[before_memory + 3]);
	uint32_t end = sys_get_le32(&out[before_memory + 7]);

	zassert_equal(start, 0x20001000);
	zassert_equal(end - start, len - MARK - before_memory - 11, "the block's end matches what was kept");
	zassert_equal((end - start) % 4, 0);
	zassert_mem_equal(&out[before_memory + 11], &dump[before_memory + 11], end - start);
	zassert_equal(tg_crash_stored_flags(), TG_CRASH_FLAG_CUT);
}

ZTEST(crash_store, test_register_block_too_large_is_left_out)
{
	static uint8_t out[STORE_SIZE];

	dump_begin();
	dump_block('A', STORE_SIZE);
	dump_memory(0x20001000, 64);
	feed(dump_len);
	zassert_equal(stored(out, sizeof(out)), 12 + 11 + 64 + MARK, "the header, the memory block and the mark");
	zassert_equal(out[12], 'M');
	zassert_equal(tg_crash_stored_flags(), TG_CRASH_FLAG_CUT);
}

ZTEST(crash_store, test_unknown_block_ends_the_dump)
{
	static uint8_t out[STORE_SIZE];
	size_t known;

	dump_begin();
	dump_block('A', 20);
	known = dump_len;
	dump_block('Q', 20);
	dump_memory(0x20001000, 64);
	feed(dump_len);
	zassert_equal(stored(out, sizeof(out)), known + MARK);
	zassert_equal(tg_crash_stored_flags(), TG_CRASH_FLAG_CUT);
}

ZTEST(crash_store, test_not_a_dump_is_not_kept)
{
	dump_begin();
	dump[0] = 'X';
	dump_block('A', 20);
	feed(dump_len);
	zassert_equal(coredump_query(COREDUMP_QUERY_HAS_STORED_DUMP, NULL), 0);

	dump_begin();
	dump[6] = 7; /* A pointer size of 128 bits. */
	dump_block('A', 20);
	feed(dump_len);
	zassert_equal(coredump_query(COREDUMP_QUERY_HAS_STORED_DUMP, NULL), 0);

	dump_begin();
	dump_len = 5;
	feed(dump_len);
	zassert_equal(coredump_query(COREDUMP_QUERY_HAS_STORED_DUMP, NULL), 0, "a header that never finished");
}

ZTEST(crash_store, test_changed_dump_is_refused)
{
	dump_begin();
	dump_block('A', 20);
	feed(dump_len);
	zassert_equal(coredump_query(COREDUMP_QUERY_HAS_STORED_DUMP, NULL), 1);
	tg_crash_ram_damage();
	zassert_equal(coredump_query(COREDUMP_QUERY_HAS_STORED_DUMP, NULL), 0, "the check found the damage");
	zassert_equal(tg_crash_pending(), 0);
}

ZTEST(crash_store, test_copy_and_erase)
{
	uint8_t out[64];
	struct coredump_cmd_copy_arg arg = {.offset = 20, .buffer = out, .length = sizeof(out)};

	dump_begin();
	dump_block('A', 20);
	feed(dump_len);
	zassert_equal(coredump_cmd(COREDUMP_CMD_COPY_STORED_DUMP, &arg), dump_len + MARK - 20, "only what is there");
	zassert_mem_equal(out, &dump[20], dump_len - 20);
	arg.offset = dump_len + MARK;
	zassert_equal(coredump_cmd(COREDUMP_CMD_COPY_STORED_DUMP, &arg), 0);
	arg.offset = -1;
	zassert_equal(coredump_cmd(COREDUMP_CMD_COPY_STORED_DUMP, &arg), -EINVAL);
	zassert_equal(coredump_cmd(COREDUMP_CMD_COPY_STORED_DUMP, NULL), -EINVAL);
	zassert_ok(coredump_cmd(COREDUMP_CMD_ERASE_STORED_DUMP, NULL));
	zassert_equal(coredump_query(COREDUMP_QUERY_HAS_STORED_DUMP, NULL), 0);
	arg.offset = 0;
	zassert_equal(coredump_cmd(COREDUMP_CMD_COPY_STORED_DUMP, &arg), -ENOENT);
}

ZTEST(crash_store, test_zephyr_writes_a_real_dump)
{
	static uint8_t out[STORE_SIZE];
	size_t len;

	/* Zephyr's own dump of this thread, without registers, since there was no fault. */
	coredump(K_ERR_KERNEL_OOPS, NULL, k_current_get());
	len = stored(out, sizeof(out));
	zassert_true(len > 12, "stored %u bytes", (unsigned int)len);
	zassert_equal(out[0], 'Z');
	zassert_equal(out[1], 'E');
	zassert_equal(sys_get_le32(&out[8]), K_ERR_KERNEL_OOPS);
	zassert_equal(out[12], 'M', "the thread's memory follows the header");
}

ZTEST_SUITE(crash_store, NULL, NULL, before, NULL, NULL);

void store_dump(void)
{
	dump_begin();
	dump_block('A', 40);
	dump_memory(0x20001000, 400);
	feed(dump_len);
	zassert_equal(tg_crash_pending(), dump_len + MARK);
}

void before_upload(void *fixture)
{
	before(fixture);
	fake_server_reset();
}

ZTEST(crash_upload, test_nothing_stored_sends_nothing)
{
	zassert_ok(send_crash());
	zassert_equal(srv.exchanges, 0);
}

ZTEST(crash_upload, test_sent_whole_then_erased)
{
	const uint8_t *own;
	size_t own_len = tg_build_id(&own);

	store_dump();
	zassert_ok(send_crash());
	zassert_true(srv.complete);
	zassert_equal(srv.have, dump_len + MARK);
	zassert_mem_equal(srv.data, dump, dump_len);
	zassert_equal(srv.chunks, DIV_ROUND_UP(dump_len + MARK, 100));
	zassert_equal(srv.build_id_len, own_len, "the build ID recorded with the crash is sent");
	zassert_mem_equal(srv.build_id, own, own_len);
	zassert_equal(tg_crash_pending(), 0, "erased once the server has it");
}

ZTEST(crash_upload, test_chunks_fit_the_device_buffer)
{
	store_dump();
	srv.chunk_max = 4096;
	zassert_ok(send_crash());
	zassert_true(srv.complete);
	zassert_equal(srv.chunks, DIV_ROUND_UP(dump_len + MARK, CONFIG_TRACEGALE_MAX_MESSAGE - 25));
}

ZTEST(crash_upload, test_dropped_connection_resumes)
{
	store_dump();
	srv.fail_exchange_at = 3;
	zassert_equal(send_crash(), -TRACEGALE_ERR_TIMEOUT);
	zassert_equal(tg_crash_pending(), dump_len + MARK, "kept until the server has all of it");
	zassert_equal(srv.have, 100);
	zassert_ok(send_crash());
	zassert_true(srv.complete);
	zassert_equal(srv.starts, 2);
	/* The chunk lost in the dropped exchange never reached the server, so nothing was sent twice. */
	zassert_equal(srv.bytes_received, dump_len + MARK);
	zassert_equal(tg_crash_pending(), 0);
}

ZTEST(crash_upload, test_lost_last_reply_finishes_without_resending)
{
	store_dump();
	/* START and five chunks: the server gets the last chunk, but its reply is lost. */
	srv.lose_reply_at = 1 + DIV_ROUND_UP(dump_len + MARK, 100);
	zassert_equal(send_crash(), -TRACEGALE_ERR_TIMEOUT);
	zassert_true(srv.complete);
	zassert_equal(tg_crash_pending(), dump_len + MARK, "the device doesn't know yet");
	srv.chunks = 0;
	zassert_ok(send_crash());
	zassert_equal(srv.chunks, 0, "the server says it's complete, so nothing is sent again");
	zassert_equal(tg_crash_pending(), 0);
}

ZTEST(crash_upload, test_identical_crash_is_sent_again)
{
	store_dump();
	zassert_ok(send_crash());
	feed(dump_len);
	srv.chunks = 0;
	zassert_ok(send_crash());
	zassert_true(srv.chunks > 0, "a second identical crash is a new upload");
	zassert_equal(tg_crash_pending(), 0);
}

ZTEST(crash_upload, test_hash_mismatch_gives_up_after_three)
{
	store_dump();
	srv.error_on_start = TG_WIRE_ERR_HASH_MISMATCH;
	zassert_equal(send_crash(), -1000 - TG_WIRE_ERR_HASH_MISMATCH);
	zassert_equal(send_crash(), -1000 - TG_WIRE_ERR_HASH_MISMATCH);
	zassert_equal(tg_crash_pending(), dump_len + MARK);
	zassert_equal(send_crash(), -1000 - TG_WIRE_ERR_HASH_MISMATCH);
	zassert_equal(tg_crash_pending(), 0);
}

ZTEST(crash_upload, test_refused_kind_is_erased)
{
	store_dump();
	srv.error_on_start = TG_WIRE_ERR_UNSUPPORTED_KIND;
	zassert_equal(send_crash(), -1000 - TG_WIRE_ERR_UNSUPPORTED_KIND);
	zassert_equal(tg_crash_pending(), 0, "this server will never take it");
}

ZTEST(crash_upload, test_busy_server_keeps_it)
{
	store_dump();
	srv.error_on_start = 12;
	zassert_equal(send_crash(), -1000 - 12);
	zassert_equal(tg_crash_pending(), dump_len + MARK);
}

ZTEST(crash_upload, test_forgotten_transfer_starts_again_once)
{
	store_dump();
	srv.forget_once = true;
	zassert_ok(send_crash());
	zassert_true(srv.complete);
	zassert_equal(srv.starts, 2);
	zassert_equal(tg_crash_pending(), 0);
}

ZTEST(crash_upload, test_stalled_server_is_a_bad_reply)
{
	store_dump();
	srv.stall = true;
	zassert_equal(send_crash(), -TRACEGALE_ERR_BAD_REPLY);
	zassert_true(srv.chunks <= 4, "gives up rather than send forever: %d", srv.chunks);
	zassert_equal(tg_crash_pending(), dump_len + MARK);
}

ZTEST(crash_upload, test_garbage_reply_keeps_it)
{
	store_dump();
	srv.garbage = true;
	zassert_equal(send_crash(), -TRACEGALE_ERR_BAD_REPLY);
	zassert_equal(tg_crash_pending(), dump_len + MARK);
}

ZTEST(crash_upload, test_zero_chunk_max_is_a_bad_reply)
{
	store_dump();
	srv.chunk_max = 0;
	zassert_equal(send_crash(), -TRACEGALE_ERR_BAD_REPLY);
	zassert_equal(tg_crash_pending(), dump_len + MARK);
}

ZTEST_SUITE(crash_upload, NULL, NULL, before_upload, NULL, NULL);
