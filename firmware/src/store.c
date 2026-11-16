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
#include <zephyr/sys/base64.h>
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
#include "core.h"
#include "der.h"
#if defined(CONFIG_TRACEGALE_ENROLL)
#include "enroll.h"
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
/* The active set is an enrollment key's claim, for one connection, and is never stored. */
static bool claim;
/* The credentials entered since the last commit, which commit checks. A stored key may be a modem's marker, not DER. */
static uint8_t changed;
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
	    settings_name_steq(key, "modem", &next) || settings_name_steq(key, "enroll", &next)) {
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
	claim = false;
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

#if defined(CONFIG_TRACEGALE_ENROLL)
/* Decodes base64 into a credential buffer. @return the length, or 0 if it isn't valid or doesn't fit. */
static uint16_t decode_into(uint8_t out[CONFIG_TRACEGALE_CRED_MAX_SIZE], const char *b64)
{
	size_t len = 0;

	if (base64_decode(out, CONFIG_TRACEGALE_CRED_MAX_SIZE, &len, b64, strlen(b64)) != 0) {
		return 0;
	}
	return (uint16_t)len;
}

int tg_store_use_claim(const char *host, uint16_t port, const char *ca_b64, const char *cert_b64, const uint8_t *key, size_t key_len)
{
	int rc = 0;

	if (strlen(host) >= sizeof(active.host) || key_len > CONFIG_TRACEGALE_CRED_MAX_SIZE) {
		return -TRACEGALE_ERR_TOO_LARGE;
	}
	k_mutex_lock(&lock, K_FOREVER);
	ready = false;
	tls_remove();
	memset(&active, 0, sizeof(active));
	strcpy(active.host, host);
	active.port = port;
	active.cred_len[TRACEGALE_CREDENTIAL_CA] = decode_into(active.cred[TRACEGALE_CREDENTIAL_CA], ca_b64);
	active.cred_len[TRACEGALE_CREDENTIAL_DEVICE_CERT] = decode_into(active.cred[TRACEGALE_CREDENTIAL_DEVICE_CERT], cert_b64);
	memcpy(active.cred[TRACEGALE_CREDENTIAL_DEVICE_KEY], key, key_len);
	active.cred_len[TRACEGALE_CREDENTIAL_DEVICE_KEY] = key_len;
	claim = true;
	if (active.cred_len[TRACEGALE_CREDENTIAL_CA] == 0 || active.cred_len[TRACEGALE_CREDENTIAL_DEVICE_CERT] == 0) {
		rc = -TRACEGALE_ERR_BAD_ARGUMENT;
	} else if (tls_install() != 0) {
		rc = -TRACEGALE_ERR_STORAGE;
	}
	k_mutex_unlock(&lock);
	return rc;
}

int tg_store_pending_b64(enum tracegale_credential type, const char *b64)
{
	k_mutex_lock(&lock, K_FOREVER);
	pending.cred_len[type] = decode_into(pending.cred[type], b64);
	changed |= BIT(type);
	int rc = pending.cred_len[type] > 0 ? 0 : -TRACEGALE_ERR_BAD_ARGUMENT;

	k_mutex_unlock(&lock);
	return rc;
}
#endif

#if defined(CONFIG_TRACEGALE_ENROLL)
void tg_store_drop_claim(void)
{
	k_mutex_lock(&lock, K_FOREVER);
	if (claim) {
		tls_remove();
		/* It held the claim certificate's private key. */
		memset(&active, 0, sizeof(active));
		claim = false;
	}
	k_mutex_unlock(&lock);
}
#endif

int tracegale_provision_server(const char *host, uint16_t port)
{
	if (host == NULL || host[0] == '\0' || strlen(host) >= sizeof(pending.host)) {
		return -TRACEGALE_ERR_BAD_ARGUMENT;
	}
	if (port == 0) {
		/* The port for this firmware's binding. */
		port = CONFIG_TRACEGALE_SERVER_PORT;
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
	changed |= BIT(type);
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
		changed |= BIT(type);
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
		changed |= BIT(type);
		k_mutex_unlock(&lock);
	}
}

size_t tg_store_pending_len(enum tracegale_credential type)
{
	return type < TRACEGALE_CREDENTIAL_COUNT ? pending.cred_len[type] : 0;
}

/*
 * Checks the credentials entered since the last commit, before anything is stored: each must be one whole DER element, and
 * the key must belong to the certificate. A piece pasted twice or left out is caught here, rather than as a TLS failure.
 */
static int check_changed(void)
{
	const uint8_t *cert_key, *key_key;
	size_t cert_key_len, key_key_len;

	for (int i = 0; i < TRACEGALE_CREDENTIAL_COUNT; i++) {
		if ((changed & BIT(i)) && pending.cred_len[i] > 0 && tg_der_whole(pending.cred[i], pending.cred_len[i]) != 0) {
			LOG_ERR("The %s isn't one whole DER element: a piece is missing or was entered twice", cred_names[i]);
			return -TRACEGALE_ERR_BAD_ARGUMENT;
		}
	}
	if (!(changed & (BIT(TRACEGALE_CREDENTIAL_DEVICE_CERT) | BIT(TRACEGALE_CREDENTIAL_DEVICE_KEY))) ||
	    pending.cred_len[TRACEGALE_CREDENTIAL_DEVICE_CERT] == 0 || pending.cred_len[TRACEGALE_CREDENTIAL_DEVICE_KEY] == 0 ||
	    tg_der_whole(pending.cred[TRACEGALE_CREDENTIAL_DEVICE_KEY], pending.cred_len[TRACEGALE_CREDENTIAL_DEVICE_KEY]) != 0) {
		/* Nothing to compare, or the key is a modem's marker, whose key the modem already checked against a certificate. */
		return 0;
	}
	if (tg_der_cert_public_key(pending.cred[TRACEGALE_CREDENTIAL_DEVICE_CERT], pending.cred_len[TRACEGALE_CREDENTIAL_DEVICE_CERT],
				   &cert_key, &cert_key_len) != 0) {
		LOG_ERR("The device certificate can't be read");
		return -TRACEGALE_ERR_BAD_ARGUMENT;
	}
	int found = tg_der_private_key_public_key(pending.cred[TRACEGALE_CREDENTIAL_DEVICE_KEY],
						  pending.cred_len[TRACEGALE_CREDENTIAL_DEVICE_KEY], &key_key, &key_key_len);

	if (found < 0) {
		LOG_ERR("The private key can't be read");
		return -TRACEGALE_ERR_BAD_ARGUMENT;
	}
	/* A key without its public half can't be compared here; TLS still refuses a pair that doesn't match. */
	if (found == 0 && (cert_key_len != key_key_len || memcmp(cert_key, key_key, key_key_len) != 0)) {
		LOG_ERR("The private key doesn't belong to the device certificate");
		return -TRACEGALE_ERR_BAD_ARGUMENT;
	}
	return 0;
}

/* An MQTT device given the HTTPS port, or the reverse, fails with only a connect error, so it's said here. */
static void check_port(uint16_t port)
{
	static const uint16_t https = CONFIG_TRACEGALE_SERVER_PORT_HTTPS, mqtt = CONFIG_TRACEGALE_SERVER_PORT_MQTT;
	bool is_mqtt = IS_ENABLED(CONFIG_TRACEGALE_BINDING_MQTT);

	if (https != mqtt && port == (is_mqtt ? https : mqtt)) {
		LOG_WRN("Port %u is the server's %s port, and this firmware speaks %s, normally on port %u", port,
			is_mqtt ? "HTTPS" : "MQTT", is_mqtt ? "MQTT" : "HTTPS", is_mqtt ? mqtt : https);
	}
}

int tracegale_provision_commit(void)
{
	int rc;

	k_mutex_lock(&attempt_lock, K_FOREVER);
	k_mutex_lock(&lock, K_FOREVER);
	rc = check_changed();
	if (rc == 0) {
		rc = use_pending();
	}
	if (rc == 0) {
		changed = 0;
		check_port(active.port);
	}
	if (rc == 0 && save() != 0) {
		LOG_ERR("Can't store the provisioned values");
		rc = -TRACEGALE_ERR_STORAGE;
	}
	k_mutex_unlock(&lock);
	k_mutex_unlock(&attempt_lock);
	if (rc == 0) {
		tg_core_wake();
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
	changed = 0;
#if defined(CONFIG_TRACEGALE_ENROLL)
	tg_enroll_forget();
#endif
	if (forget() != 0) {
		rc = -TRACEGALE_ERR_STORAGE;
	}
	k_mutex_unlock(&lock);
	k_mutex_unlock(&attempt_lock);
	return rc;
}
