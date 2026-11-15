/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * Holds the server address and device credentials, which are always provided at runtime and never compiled in.
 */

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zephyr/toolchain.h>

#if defined(CONFIG_SETTINGS)
#include <zephyr/settings/settings.h>
#endif
#include "compat.h"
#if defined(CONFIG_TRACEGALE_TLS)
#include "cred.h"
#endif
#if defined(CONFIG_TRACEGALE_CRASH_KEEP)
#include "crash.h"
#endif
#if defined(CONFIG_TRACEGALE_OTA)
#include "ota.h"
#endif
#include "store.h"

LOG_MODULE_DECLARE(tracegale, CONFIG_TRACEGALE_LOG_LEVEL);

struct values {
	char host[CONFIG_TRACEGALE_HOST_MAX_LEN + 1];
	uint16_t port;
	uint16_t cred_len[TRACEGALE_CREDENTIAL_COUNT];
	uint8_t cred[TRACEGALE_CREDENTIAL_COUNT][CONFIG_TRACEGALE_CRED_MAX_SIZE];
};

/* TLS keeps pointers into the active set, so a change is prepared separately and copied in by commit. */
static struct values active;
static struct values pending;
static bool ready;
static K_MUTEX_DEFINE(lock);
/* Held across each connection attempt, so credentials can't change while TLS is reading them. */
static K_MUTEX_DEFINE(attempt_lock);

__maybe_unused static const char *const cred_names[TRACEGALE_CREDENTIAL_COUNT] = {"ca", "cert", "key"};

static bool complete(const struct values *v)
{
	if (v->host[0] == '\0' || v->port == 0) {
		return false;
	}
	for (int i = 0; i < TRACEGALE_CREDENTIAL_COUNT; i++) {
		if (v->cred_len[i] == 0) {
			return false;
		}
	}
	return true;
}

#if defined(CONFIG_TRACEGALE_TLS)
static int tls_install(void)
{
	int rc = tg_cred_install(active.cred, active.cred_len);

	if (rc == 0) {
		/* Where a modem now holds the key, neither copy here needs it, and both lose it before anything is saved. */
		tg_cred_forget_key(active.cred[TRACEGALE_CREDENTIAL_DEVICE_KEY], &active.cred_len[TRACEGALE_CREDENTIAL_DEVICE_KEY]);
		tg_cred_forget_key(pending.cred[TRACEGALE_CREDENTIAL_DEVICE_KEY], &pending.cred_len[TRACEGALE_CREDENTIAL_DEVICE_KEY]);
	}
	return rc;
}

static void tls_remove(void)
{
	tg_cred_release();
}

static int tls_erase(void)
{
	return tg_cred_erase();
}
#else
static void tls_remove(void)
{
}

static int tls_install(void)
{
	return 0;
}

static int tls_erase(void)
{
	return 0;
}
#endif

#if defined(CONFIG_SETTINGS)
static int on_setting(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	const char *next;
	void *target = NULL;
	size_t cap = 0;

	if (settings_name_steq(key, "crash", &next) || settings_name_steq(key, "ota", &next) ||
	    settings_name_steq(key, "modem", &next)) {
		/* A kept crash, an update's state, and what a modem holds, which their own code reads by itself. */
		return 0;
	}
	if (settings_name_steq(key, "host", &next) && next == NULL) {
		target = pending.host;
		cap = sizeof(pending.host) - 1;
	} else if (settings_name_steq(key, "port", &next) && next == NULL) {
		target = &pending.port;
		cap = sizeof(pending.port);
	} else {
		for (int i = 0; i < TRACEGALE_CREDENTIAL_COUNT; i++) {
			if (settings_name_steq(key, cred_names[i], &next) && next == NULL) {
				target = pending.cred[i];
				cap = sizeof(pending.cred[i]);
				pending.cred_len[i] = 0;
				if (len <= cap) {
					pending.cred_len[i] = len;
				}
				break;
			}
		}
	}
	if (target == NULL || len > cap) {
		return -ENOENT;
	}
	return read_cb(cb_arg, target, len) < 0 ? -EIO : 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(tracegale, "tracegale", NULL, on_setting, NULL, NULL);

static int save(void)
{
	int rc = settings_save_one("tracegale/host", active.host, strlen(active.host));

	if (rc == 0) {
		rc = settings_save_one("tracegale/port", &active.port, sizeof(active.port));
	}
	for (int i = 0; rc == 0 && i < TRACEGALE_CREDENTIAL_COUNT; i++) {
		char name[24];

		snprintf(name, sizeof(name), "tracegale/%s", cred_names[i]);
		rc = settings_save_one(name, active.cred[i], active.cred_len[i]);
	}
	return rc;
}

static int forget(void)
{
	int rc = settings_delete("tracegale/host");

	if (rc == 0) {
		rc = settings_delete("tracegale/port");
	}
	for (int i = 0; rc == 0 && i < TRACEGALE_CREDENTIAL_COUNT; i++) {
		char name[24];

		snprintf(name, sizeof(name), "tracegale/%s", cred_names[i]);
		rc = settings_delete(name);
	}
#if defined(CONFIG_TRACEGALE_CRASH_KEEP)
	if (rc == 0) {
		/* It holds this device's memory, so it goes with the credentials. */
		rc = tg_crash_kept_forget();
	}
#endif
	return rc;
}
#else
static int save(void)
{
	return 0;
}

static int forget(void)
{
	return 0;
}
#endif

/* Caller holds the lock. */
static int use_pending(void)
{
	if (!complete(&pending)) {
		return -TRACEGALE_ERR_NOT_PROVISIONED;
	}
	ready = false;
	tls_remove();
	active = pending;
	if (tls_install() != 0) {
		return -TRACEGALE_ERR_STORAGE;
	}
	ready = true;
	return 0;
}

void tg_store_load(void)
{
#if defined(CONFIG_SETTINGS)
	int rc = settings_subsys_init();

	if (rc == 0) {
		rc = settings_load_subtree("tracegale");
	}
	if (rc != 0) {
		LOG_ERR("Can't load stored settings: %d", rc);
		return;
	}
#if defined(CONFIG_TRACEGALE_CRASH_KEEP)
	tg_crash_kept_load();
#endif
#if defined(CONFIG_TRACEGALE_OTA)
	tg_ota_boot();
#endif
	k_mutex_lock(&lock, K_FOREVER);
	if (complete(&pending)) {
		(void)use_pending();
	}
	k_mutex_unlock(&lock);
#endif
}

void tg_store_attempt_begin(void)
{
	k_mutex_lock(&attempt_lock, K_FOREVER);
}

void tg_store_attempt_end(void)
{
	k_mutex_unlock(&attempt_lock);
}

bool tg_store_ready(void)
{
	return ready;
}

const uint8_t *tg_store_credential(enum tracegale_credential type, size_t *len)
{
	if (type >= TRACEGALE_CREDENTIAL_COUNT || active.cred_len[type] == 0) {
		*len = 0;
		return NULL;
	}
	*len = active.cred_len[type];
	return active.cred[type];
}

const char *tg_store_host(void)
{
	return active.host;
}

uint16_t tg_store_port(void)
{
	return active.port;
}

int tracegale_provision_server(const char *host, uint16_t port)
{
	if (host == NULL || host[0] == '\0' || strlen(host) >= sizeof(pending.host) || port == 0) {
		return -TRACEGALE_ERR_BAD_ARGUMENT;
	}
	k_mutex_lock(&lock, K_FOREVER);
	strcpy(pending.host, host);
	pending.port = port;
	k_mutex_unlock(&lock);
	return 0;
}

int tracegale_provision_credential(enum tracegale_credential type, const uint8_t *der, size_t len)
{
	if (type >= TRACEGALE_CREDENTIAL_COUNT || der == NULL || len == 0) {
		return -TRACEGALE_ERR_BAD_ARGUMENT;
	}
	if (len > CONFIG_TRACEGALE_CRED_MAX_SIZE) {
		return -TRACEGALE_ERR_TOO_LARGE;
	}
	k_mutex_lock(&lock, K_FOREVER);
	memcpy(pending.cred[type], der, len);
	pending.cred_len[type] = len;
	k_mutex_unlock(&lock);
	return 0;
}

int tg_store_append(enum tracegale_credential type, const uint8_t *data, size_t len)
{
	int rc = 0;

	if (type >= TRACEGALE_CREDENTIAL_COUNT) {
		return -TRACEGALE_ERR_BAD_ARGUMENT;
	}
	k_mutex_lock(&lock, K_FOREVER);
	if (len > sizeof(pending.cred[type]) - pending.cred_len[type]) {
		rc = -TRACEGALE_ERR_TOO_LARGE;
	} else {
		memcpy(&pending.cred[type][pending.cred_len[type]], data, len);
		pending.cred_len[type] += len;
	}
	k_mutex_unlock(&lock);
	return rc;
}

void tg_store_clear_pending(enum tracegale_credential type)
{
	if (type < TRACEGALE_CREDENTIAL_COUNT) {
		k_mutex_lock(&lock, K_FOREVER);
		memset(pending.cred[type], 0, sizeof(pending.cred[type]));
		pending.cred_len[type] = 0;
		k_mutex_unlock(&lock);
	}
}

size_t tg_store_pending_len(enum tracegale_credential type)
{
	return type < TRACEGALE_CREDENTIAL_COUNT ? pending.cred_len[type] : 0;
}

int tracegale_provision_commit(void)
{
	int rc;

	k_mutex_lock(&attempt_lock, K_FOREVER);
	k_mutex_lock(&lock, K_FOREVER);
	rc = use_pending();
	if (rc == 0 && save() != 0) {
		LOG_ERR("Can't store the provisioned values");
		rc = -TRACEGALE_ERR_STORAGE;
	}
	k_mutex_unlock(&lock);
	k_mutex_unlock(&attempt_lock);
	if (rc == 0) {
		tracegale_connect_now();
	}
	return rc;
}

int tracegale_provision_erase(void)
{
	int rc;

	k_mutex_lock(&attempt_lock, K_FOREVER);
	k_mutex_lock(&lock, K_FOREVER);
	ready = false;
	rc = tls_erase();
	memset(&active, 0, sizeof(active));
	memset(&pending, 0, sizeof(pending));
	if (forget() != 0) {
		rc = -TRACEGALE_ERR_STORAGE;
	}
	k_mutex_unlock(&lock);
	k_mutex_unlock(&attempt_lock);
	return rc;
}
