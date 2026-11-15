/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tests the credentials backend for Nordic's modems (cred_nrf_modem.c) against a fake modem: the PEM it writes, that it writes
 * only when something changed, what happens when the modem refuses because its link is up, and erasing.
 */

#include <errno.h>
#include <string.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/base64.h>
#include <zephyr/ztest.h>

#include <modem/modem_key_mgmt.h>

#include "cred.h"

#define TYPES	   3
#define STORE_SIZE 1024

/* The fake modem. */
static struct {
	char data[STORE_SIZE];
	size_t len;
	bool present;
} slot[TYPES];
static int writes;
static int deletes;
/* Returned by a write or delete once `allowed` more have succeeded, as a modem with its link up returns -EPERM. */
static int refusal;
static int allowed;

static int refused(void)
{
	if (refusal != 0) {
		if (allowed == 0) {
			return refusal;
		}
		allowed--;
	}
	return 0;
}

int modem_key_mgmt_write(nrf_sec_tag_t sec_tag, enum modem_key_mgmt_cred_type cred_type, const void *buf, size_t len)
{
	int rc = refused();

	zassert_equal(sec_tag, CONFIG_TRACEGALE_SEC_TAG);
	zassert_true(cred_type < TYPES);
	if (rc != 0) {
		return rc;
	}
	zassert_true(len <= STORE_SIZE);
	memcpy(slot[cred_type].data, buf, len);
	slot[cred_type].len = len;
	slot[cred_type].present = true;
	writes++;
	return 0;
}

int modem_key_mgmt_delete(nrf_sec_tag_t sec_tag, enum modem_key_mgmt_cred_type cred_type)
{
	int rc = refused();

	zassert_equal(sec_tag, CONFIG_TRACEGALE_SEC_TAG);
	if (rc != 0) {
		return rc;
	}
	if (!slot[cred_type].present) {
		return -ENOENT;
	}
	slot[cred_type].present = false;
	deletes++;
	return 0;
}

int modem_key_mgmt_exists(nrf_sec_tag_t sec_tag, enum modem_key_mgmt_cred_type cred_type, bool *exists)
{
	zassert_equal(sec_tag, CONFIG_TRACEGALE_SEC_TAG);
	*exists = slot[cred_type].present;
	return 0;
}

static uint8_t cred[TRACEGALE_CREDENTIAL_COUNT][CONFIG_TRACEGALE_CRED_MAX_SIZE];
static uint16_t len[TRACEGALE_CREDENTIAL_COUNT];

static void fill(uint8_t seed)
{
	static const uint16_t sizes[TRACEGALE_CREDENTIAL_COUNT] = {300, 301, 138};

	for (int i = 0; i < TRACEGALE_CREDENTIAL_COUNT; i++) {
		len[i] = sizes[i];
		for (int j = 0; j < sizes[i]; j++) {
			cred[i][j] = (uint8_t)(seed + i * 31 + j * 7);
		}
	}
}

static bool mark_found;

static int mark_read(const char *key, size_t size, settings_read_cb read_cb, void *cb_arg, void *param)
{
	ARG_UNUSED(read_cb);
	ARG_UNUSED(cb_arg);
	ARG_UNUSED(param);
	mark_found = key == NULL && size == 32;
	return 0;
}

static bool mark_stored(void)
{
	mark_found = false;
	zassert_ok(settings_load_subtree_direct("tracegale/modem", mark_read, NULL));
	return mark_found;
}

/* Checks that the modem holds `der` as PEM with this label, in lines of 64 characters. */
static void check_pem(int type, const char *label, const uint8_t *der, size_t der_len)
{
	char begin[40];
	char end[40];
	char body[STORE_SIZE];
	uint8_t decoded[STORE_SIZE];
	size_t body_len = 0;
	size_t decoded_len;
	const char *p = slot[type].data;
	const char *stop = slot[type].data + slot[type].len;

	zassert_true(slot[type].present);
	snprintk(begin, sizeof(begin), "-----BEGIN %s-----\n", label);
	snprintk(end, sizeof(end), "-----END %s-----\n", label);
	zassert_true(slot[type].len > strlen(begin) + strlen(end));
	zassert_mem_equal(p, begin, strlen(begin));
	zassert_mem_equal(stop - strlen(end), end, strlen(end));
	p += strlen(begin);
	stop -= strlen(end);
	while (p < stop) {
		const char *nl = memchr(p, '\n', stop - p);

		zassert_not_null(nl, "every line ends with a newline");
		zassert_true(nl - p <= 64, "lines are at most 64 characters");
		zassert_true(nl == stop - 1 || nl - p == 64, "only the last line is short");
		memcpy(&body[body_len], p, nl - p);
		body_len += nl - p;
		p = nl + 1;
	}
	zassert_ok(base64_decode(decoded, sizeof(decoded), &decoded_len, (const uint8_t *)body, body_len));
	zassert_equal(decoded_len, der_len);
	zassert_mem_equal(decoded, der, der_len);
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	refusal = 0;
	(void)tg_cred_erase();
	memset(slot, 0, sizeof(slot));
	writes = 0;
	deletes = 0;
	fill(1);
}

static void *setup(void)
{
	zassert_ok(settings_subsys_init());
	return NULL;
}

ZTEST(cred_modem, test_writes_pem_of_each_type)
{
	zassert_ok(tg_cred_install(cred, len));
	zassert_equal(writes, 3);
	check_pem(MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN, "CERTIFICATE", cred[TRACEGALE_CREDENTIAL_CA], len[TRACEGALE_CREDENTIAL_CA]);
	check_pem(MODEM_KEY_MGMT_CRED_TYPE_PUBLIC_CERT, "CERTIFICATE", cred[TRACEGALE_CREDENTIAL_DEVICE_CERT],
		  len[TRACEGALE_CREDENTIAL_DEVICE_CERT]);
	check_pem(MODEM_KEY_MGMT_CRED_TYPE_PRIVATE_CERT, "PRIVATE KEY", cred[TRACEGALE_CREDENTIAL_DEVICE_KEY],
		  len[TRACEGALE_CREDENTIAL_DEVICE_KEY]);
	zassert_true(mark_stored());
}

ZTEST(cred_modem, test_largest_credentials_fit)
{
	for (int i = 0; i < TRACEGALE_CREDENTIAL_COUNT; i++) {
		len[i] = CONFIG_TRACEGALE_CRED_MAX_SIZE;
		memset(cred[i], 0xa5 + i, CONFIG_TRACEGALE_CRED_MAX_SIZE);
	}
	zassert_ok(tg_cred_install(cred, len));
	check_pem(MODEM_KEY_MGMT_CRED_TYPE_PRIVATE_CERT, "PRIVATE KEY", cred[TRACEGALE_CREDENTIAL_DEVICE_KEY],
		  CONFIG_TRACEGALE_CRED_MAX_SIZE);
}

ZTEST(cred_modem, test_same_credentials_are_not_written_again)
{
	zassert_ok(tg_cred_install(cred, len));
	zassert_ok(tg_cred_install(cred, len));
	zassert_equal(writes, 3, "a restart with the same credentials writes nothing");
	/* Nor does it need the link down: the modem would refuse a write now. */
	refusal = -EPERM;
	zassert_ok(tg_cred_install(cred, len));
}

ZTEST(cred_modem, test_changed_credentials_are_written)
{
	zassert_ok(tg_cred_install(cred, len));
	fill(2);
	zassert_ok(tg_cred_install(cred, len));
	zassert_equal(writes, 6);
	check_pem(MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN, "CERTIFICATE", cred[TRACEGALE_CREDENTIAL_CA], len[TRACEGALE_CREDENTIAL_CA]);
}

ZTEST(cred_modem, test_a_missing_credential_is_written_again)
{
	zassert_ok(tg_cred_install(cred, len));
	slot[MODEM_KEY_MGMT_CRED_TYPE_PUBLIC_CERT].present = false;
	zassert_ok(tg_cred_install(cred, len));
	zassert_equal(writes, 6);
}

ZTEST(cred_modem, test_a_refusal_part_way_leaves_nothing_trusted)
{
	zassert_ok(tg_cred_install(cred, len));
	fill(3);
	/* The modem takes the first credential, then its link comes up. */
	refusal = -EPERM;
	allowed = 1;
	zassert_equal(tg_cred_install(cred, len), -TRACEGALE_ERR_STORAGE);
	zassert_false(mark_stored(), "a mixed set isn't remembered as written");
	/* Back to the first set, which the modem no longer fully holds: it has to be written again. */
	refusal = 0;
	fill(1);
	writes = 0;
	zassert_ok(tg_cred_install(cred, len));
	zassert_equal(writes, 3);
	zassert_true(mark_stored());
}

ZTEST(cred_modem, test_erase_removes_everything)
{
	zassert_ok(tg_cred_install(cred, len));
	zassert_ok(tg_cred_erase());
	zassert_equal(deletes, 3);
	for (int i = 0; i < TYPES; i++) {
		zassert_false(slot[i].present);
	}
	zassert_false(mark_stored());
	zassert_ok(tg_cred_erase(), "erasing again finds nothing, which is fine");
}

ZTEST(cred_modem, test_erase_reports_a_refusal)
{
	zassert_ok(tg_cred_install(cred, len));
	refusal = -EPERM;
	zassert_equal(tg_cred_erase(), -TRACEGALE_ERR_STORAGE);
	zassert_false(mark_stored(), "the credentials are written again at the next install");
	refusal = 0;
	writes = 0;
	zassert_ok(tg_cred_install(cred, len));
	zassert_equal(writes, 3);
}

ZTEST(cred_modem, test_the_key_is_forgotten_once_the_modem_has_it)
{
	uint8_t original[CONFIG_TRACEGALE_CRED_MAX_SIZE];
	uint16_t original_len = len[TRACEGALE_CREDENTIAL_DEVICE_KEY];

	memcpy(original, cred[TRACEGALE_CREDENTIAL_DEVICE_KEY], sizeof(original));
	zassert_ok(tg_cred_install(cred, len));
	tg_cred_forget_key(cred[TRACEGALE_CREDENTIAL_DEVICE_KEY], &len[TRACEGALE_CREDENTIAL_DEVICE_KEY]);
	zassert_equal(len[TRACEGALE_CREDENTIAL_DEVICE_KEY], 36, "a marker of a tag and a hash");
	zassert_mem_equal(cred[TRACEGALE_CREDENTIAL_DEVICE_KEY], "TGK1", 4);
	for (size_t i = 4; i + 8 <= original_len; i++) {
		zassert_true(memcmp(&cred[TRACEGALE_CREDENTIAL_DEVICE_KEY][i], &original[i], 8) != 0,
			     "no part of the key may remain");
	}
	/* Forgetting again changes nothing, and a restart with the marker needs no write. */
	tg_cred_forget_key(cred[TRACEGALE_CREDENTIAL_DEVICE_KEY], &len[TRACEGALE_CREDENTIAL_DEVICE_KEY]);
	zassert_equal(len[TRACEGALE_CREDENTIAL_DEVICE_KEY], 36);
	writes = 0;
	refusal = -EPERM;
	zassert_ok(tg_cred_install(cred, len));
	zassert_equal(writes, 0);
}

ZTEST(cred_modem, test_a_modem_that_lost_the_key_needs_provisioning)
{
	zassert_ok(tg_cred_install(cred, len));
	tg_cred_forget_key(cred[TRACEGALE_CREDENTIAL_DEVICE_KEY], &len[TRACEGALE_CREDENTIAL_DEVICE_KEY]);
	slot[MODEM_KEY_MGMT_CRED_TYPE_PRIVATE_CERT].present = false;
	writes = 0;
	zassert_equal(tg_cred_install(cred, len), -TRACEGALE_ERR_STORAGE, "the device has no key to write back");
	zassert_equal(writes, 0, "and must not write the marker as a key");
}

ZTEST_SUITE(cred_modem, NULL, setup, before, NULL, NULL);
