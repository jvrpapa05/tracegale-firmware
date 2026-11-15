/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * Credentials in the storage of Nordic's cellular modems, whose TLS reads them there. nRF Connect SDK only
 * (CONFIG_TRACEGALE_CRED_NRF_MODEM). Only the SDK's public modem_key_mgmt API is used.
 *
 * The modem takes PEM text, and accepts writes only while its link is off. A private key can't be read back, so to know what it
 * already holds, this keeps a SHA-256 of the set it last wrote in settings storage, and writes again only when that changes or a
 * credential is missing. A device that restarts with its link already up then needs no write, and the modem's flash isn't
 * rewritten at every boot.
 *
 * Once the modem holds the private key, the store's copy is replaced by a marker with only the key's hash
 * (tg_cred_forget_key), so the key isn't kept in plain flash or memory. The price: if the modem ever loses its credentials,
 * the device can't put the key back, and has to be provisioned again.
 */

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/base64.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <modem/modem_key_mgmt.h>

#if defined(CONFIG_SETTINGS)
#include <zephyr/settings/settings.h>
#endif

#include "cred.h"
#include "sha256.h"

LOG_MODULE_DECLARE(tracegale, CONFIG_TRACEGALE_LOG_LEVEL);

#define MARK_NAME "tracegale/modem"

/* 48 bytes make one 64-character line of base64. */
#define PEM_LINE_BYTES 48
#define BASE64_LEN(n)  ((((n) + 2) / 3) * 4)
/* The base64, a newline per line, the longest header and footer (54 characters), and the terminator base64_encode writes. */
#define PEM_SIZE                                                                                                               \
	(BASE64_LEN(CONFIG_TRACEGALE_CRED_MAX_SIZE) + DIV_ROUND_UP(CONFIG_TRACEGALE_CRED_MAX_SIZE, PEM_LINE_BYTES) + 54 + 1)

static const enum modem_key_mgmt_cred_type types[TRACEGALE_CREDENTIAL_COUNT] = {
	MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN, MODEM_KEY_MGMT_CRED_TYPE_PUBLIC_CERT, MODEM_KEY_MGMT_CRED_TYPE_PRIVATE_CERT};
/* The server issues the key as PKCS#8, which PEM labels "PRIVATE KEY". */
static const char *const labels[TRACEGALE_CREDENTIAL_COUNT] = {"CERTIFICATE", "CERTIFICATE", "PRIVATE KEY"};
static const char *const names[TRACEGALE_CREDENTIAL_COUNT] = {"ca", "cert", "key"};

/* Used only with the store's lock held. */
static char pem[PEM_SIZE];
static uint8_t mark[TG_SHA256_SIZE];
static bool mark_known;

/* @return the PEM text's length, or 0 if it doesn't fit, which the store's size limit prevents. */
static size_t to_pem(const char *label, const uint8_t *der, size_t len)
{
	char *p = pem;
	char *end = pem + sizeof(pem);
	int n = snprintk(p, end - p, "-----BEGIN %s-----\n", label);

	if (n < 0 || n >= end - p) {
		return 0;
	}
	p += n;
	for (size_t off = 0; off < len; off += PEM_LINE_BYTES) {
		size_t written;

		if (base64_encode((uint8_t *)p, end - p, &written, der + off, MIN(PEM_LINE_BYTES, len - off)) != 0 ||
		    written + 1 >= (size_t)(end - p)) {
			return 0;
		}
		p += written;
		*p++ = '\n';
	}
	n = snprintk(p, end - p, "-----END %s-----\n", label);
	if (n < 0 || n >= end - p) {
		return 0;
	}
	return (p - pem) + n;
}

/* What replaces the key once the modem holds it: these 4 bytes, then the key's SHA-256. A PKCS#8 key starts with 0x30. */
static const uint8_t key_mark_tag[4] = {'T', 'G', 'K', '1'};
#define KEY_MARK_LEN (sizeof(key_mark_tag) + TG_SHA256_SIZE)

static bool is_key_mark(const uint8_t *key, size_t len)
{
	return len == KEY_MARK_LEN && memcmp(key, key_mark_tag, sizeof(key_mark_tag)) == 0;
}

static void sha256_of(const uint8_t *data, size_t len, uint8_t out[TG_SHA256_SIZE])
{
	struct tg_sha256 ctx;

	tg_sha256_init(&ctx);
	tg_sha256_update(&ctx, data, len);
	tg_sha256_final(&ctx, out);
}

/* The record of a set: the security tag and each credential's hash, the key's taken from its marker once it's forgotten. */
static void digest(const uint8_t cred[][CONFIG_TRACEGALE_CRED_MAX_SIZE], const uint16_t len[], uint8_t out[TG_SHA256_SIZE])
{
	struct tg_sha256 ctx;
	uint8_t field[4];
	uint8_t hash[TG_SHA256_SIZE];

	tg_sha256_init(&ctx);
	sys_put_le32(CONFIG_TRACEGALE_SEC_TAG, field);
	tg_sha256_update(&ctx, field, sizeof(field));
	for (int i = 0; i < TRACEGALE_CREDENTIAL_COUNT; i++) {
		if (i == TRACEGALE_CREDENTIAL_DEVICE_KEY && is_key_mark(cred[i], len[i])) {
			memcpy(hash, &cred[i][sizeof(key_mark_tag)], sizeof(hash));
		} else {
			sha256_of(cred[i], len[i], hash);
		}
		tg_sha256_update(&ctx, hash, sizeof(hash));
	}
	tg_sha256_final(&ctx, out);
}

#if defined(CONFIG_SETTINGS)
static int mark_read(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg, void *param)
{
	ARG_UNUSED(param);
	if (key == NULL && len == sizeof(mark) && read_cb(cb_arg, mark, sizeof(mark)) == sizeof(mark)) {
		mark_known = true;
	}
	return 0;
}

static void mark_load(void)
{
	static bool loaded;

	if (!loaded) {
		loaded = true;
		(void)settings_load_subtree_direct(MARK_NAME, mark_read, NULL);
	}
}

static int mark_save(void)
{
	return settings_save_one(MARK_NAME, mark, sizeof(mark));
}

static void mark_forget(void)
{
	(void)settings_delete(MARK_NAME);
}
#else
/* Without settings storage the mark lasts until a restart, so credentials provisioned at each boot are written each time. */
static void mark_load(void)
{
}

static int mark_save(void)
{
	return 0;
}

static void mark_forget(void)
{
}
#endif

static bool all_present(void)
{
	for (int i = 0; i < TRACEGALE_CREDENTIAL_COUNT; i++) {
		bool exists = false;

		if (modem_key_mgmt_exists(CONFIG_TRACEGALE_SEC_TAG, types[i], &exists) != 0 || !exists) {
			return false;
		}
	}
	return true;
}

static void explain(int rc)
{
	if (rc == -EPERM) {
		LOG_ERR("The modem changes credentials only while its link is off. Provision or erase before the application "
			"connects, or with the link off");
	}
}

int tg_cred_install(const uint8_t cred[TRACEGALE_CREDENTIAL_COUNT][CONFIG_TRACEGALE_CRED_MAX_SIZE],
		    const uint16_t len[TRACEGALE_CREDENTIAL_COUNT])
{
	uint8_t wanted[TG_SHA256_SIZE];

	digest(cred, len, wanted);
	mark_load();
	if (mark_known && memcmp(mark, wanted, sizeof(mark)) == 0 && all_present()) {
		return 0;
	}
	if (is_key_mark(cred[TRACEGALE_CREDENTIAL_DEVICE_KEY], len[TRACEGALE_CREDENTIAL_DEVICE_KEY])) {
		LOG_ERR("The modem no longer holds this device's credentials, and the device keeps no copy of its private key. "
			"Provision it again");
		return -TRACEGALE_ERR_STORAGE;
	}
	/* Forgotten first, so that a power loss part way through means writing them all again, not trusting a mixed set. */
	mark_known = false;
	mark_forget();
	for (int i = 0; i < TRACEGALE_CREDENTIAL_COUNT; i++) {
		size_t n = to_pem(labels[i], cred[i], len[i]);
		int rc = n == 0 ? -ENOMEM : modem_key_mgmt_write(CONFIG_TRACEGALE_SEC_TAG, types[i], pem, n);

		if (rc != 0) {
			LOG_ERR("Can't write credential %s to the modem: %d", names[i], rc);
			explain(rc);
			return -TRACEGALE_ERR_STORAGE;
		}
	}
	memcpy(mark, wanted, sizeof(mark));
	mark_known = true;
	if (mark_save() != 0) {
		/* The credentials work. Without the mark, the next boot writes them again. */
		LOG_WRN("Can't remember what the modem holds");
	}
	return 0;
}

void tg_cred_forget_key(uint8_t key[CONFIG_TRACEGALE_CRED_MAX_SIZE], uint16_t *len)
{
	uint8_t hash[TG_SHA256_SIZE];

	if (is_key_mark(key, *len)) {
		return;
	}
	sha256_of(key, *len, hash);
	/* The whole buffer is cleared, not just the key's length. It's the store's static memory, so this isn't optimised away. */
	memset(key, 0, CONFIG_TRACEGALE_CRED_MAX_SIZE);
	memcpy(key, key_mark_tag, sizeof(key_mark_tag));
	memcpy(&key[sizeof(key_mark_tag)], hash, sizeof(hash));
	*len = KEY_MARK_LEN;
}

void tg_cred_release(void)
{
	/* The modem keeps its own copy, so the store's memory can change freely. */
}

int tg_cred_erase(void)
{
	int result = 0;

	mark_known = false;
	mark_forget();
	for (int i = 0; i < TRACEGALE_CREDENTIAL_COUNT; i++) {
		int rc = modem_key_mgmt_delete(CONFIG_TRACEGALE_SEC_TAG, types[i]);

		if (rc != 0 && rc != -ENOENT) {
			LOG_ERR("Can't delete credential %s from the modem: %d", names[i], rc);
			explain(rc);
			result = -TRACEGALE_ERR_STORAGE;
		}
	}
	return result;
}
