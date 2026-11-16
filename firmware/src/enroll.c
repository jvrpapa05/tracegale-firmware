/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * Enrollment. The build file gives the server, its CA, and the enrollment key's claim certificate; the token is that
 * certificate's private key, as a raw P-256 scalar. The device makes its own key pair, keeps it, connects with the claim
 * certificate, and sends ENROLL with its serial number and public key. The server answers ENROLLED with the device's ID and a
 * certificate for that key, or tells it to wait for approval. The private key never leaves the device.
 */

#include <errno.h>
#include <string.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/base64.h>
#include <zephyr/sys/util.h>

#include <tracegale/tracegale.h>

#include "core.h"
#include "der.h"
#include "enroll.h"
#include "identity.h"
#include "store.h"
#include "transport.h"
#include "wire.h"

/* PSA Crypto where the build has it: Zephyr from v4.0, and the nRF Connect SDK. Otherwise Mbed TLS's own elliptic curves,
 * which Zephyr v3.7's TLS already uses.
 */
#if defined(CONFIG_PSA_CRYPTO) || defined(CONFIG_MBEDTLS_PSA_CRYPTO_C) || defined(CONFIG_NRF_SECURITY) ||                         \
	defined(CONFIG_BUILD_WITH_TFM)
#define TG_ENROLL_PSA 1
#include <psa/crypto.h>
#else
#include <mbedtls/ecp.h>
#endif

LOG_MODULE_DECLARE(tracegale, CONFIG_TRACEGALE_LOG_LEVEL);

/* ENROLLED carries the device's certificate, its ID and a few bytes of framing, in one message. */
BUILD_ASSERT(CONFIG_TRACEGALE_MAX_MESSAGE >= CONFIG_TRACEGALE_CRED_MAX_SIZE + 64,
	     "Enrollment needs CONFIG_TRACEGALE_MAX_MESSAGE of at least CONFIG_TRACEGALE_CRED_MAX_SIZE + 64");

#define SCALAR_LEN 32
#define POINT_LEN  65
#define PKCS8_LEN  138
/* The server's error code for a device that waits for approval. */
#define ERR_PENDING_APPROVAL 14

#define TOKEN_PREFIX "tge_"
/* "tge_" and 32 bytes in unpadded base64url. */
#define TOKEN_LEN (4 + 43)

static struct {
	bool loaded;
	bool have_token;
	bool have_key;
	uint8_t token[SCALAR_LEN];
	uint8_t key[SCALAR_LEN];
	char serial[65];
} st;

static K_MUTEX_DEFINE(lock);

#if !defined(TG_ENROLL_PSA)
static int random_bytes(void *ctx, unsigned char *buf, size_t len)
{
	ARG_UNUSED(ctx);
	return sys_csrand_get(buf, len) == 0 ? 0 : -1;
}
#endif

/* P-256: makes a key pair, or with generate false, works out the public key of the private key in d. */
static int keypair(uint8_t d[SCALAR_LEN], uint8_t q[POINT_LEN], bool generate)
{
#if defined(TG_ENROLL_PSA)
	psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_id_t id = 0;
	psa_status_t s;
	size_t n = 0;

	if (psa_crypto_init() != PSA_SUCCESS) {
		return -EIO;
	}
	psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
	psa_set_key_bits(&attr, 256);
	psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_EXPORT | PSA_KEY_USAGE_SIGN_HASH);
	psa_set_key_algorithm(&attr, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
	s = generate ? psa_generate_key(&attr, &id) : psa_import_key(&attr, d, SCALAR_LEN, &id);
	if (s == PSA_SUCCESS && generate) {
		s = psa_export_key(id, d, SCALAR_LEN, &n);
		if (s == PSA_SUCCESS && n != SCALAR_LEN) {
			s = PSA_ERROR_GENERIC_ERROR;
		}
	}
	if (s == PSA_SUCCESS) {
		s = psa_export_public_key(id, q, POINT_LEN, &n);
		if (s == PSA_SUCCESS && n != POINT_LEN) {
			s = PSA_ERROR_GENERIC_ERROR;
		}
	}
	if (id != 0) {
		(void)psa_destroy_key(id);
	}
	return s == PSA_SUCCESS ? 0 : -EIO;
#else
	mbedtls_ecp_group grp;
	mbedtls_mpi priv;
	mbedtls_ecp_point pub;
	size_t n = 0;
	int rc;

	mbedtls_ecp_group_init(&grp);
	mbedtls_mpi_init(&priv);
	mbedtls_ecp_point_init(&pub);
	rc = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);
	if (rc == 0) {
		rc = generate ? mbedtls_ecp_gen_privkey(&grp, &priv, random_bytes, NULL) : mbedtls_mpi_read_binary(&priv, d, SCALAR_LEN);
	}
	if (rc == 0) {
		rc = mbedtls_ecp_check_privkey(&grp, &priv);
	}
	if (rc == 0) {
		rc = mbedtls_ecp_mul(&grp, &pub, &priv, &grp.G, random_bytes, NULL);
	}
	if (rc == 0) {
		rc = mbedtls_ecp_point_write_binary(&grp, &pub, MBEDTLS_ECP_PF_UNCOMPRESSED, &n, q, POINT_LEN);
	}
	if (rc == 0 && generate) {
		rc = mbedtls_mpi_write_binary(&priv, d, SCALAR_LEN);
	}
	mbedtls_ecp_point_free(&pub);
	mbedtls_mpi_free(&priv);
	mbedtls_ecp_group_free(&grp);
	return rc == 0 && n == POINT_LEN ? 0 : -EIO;
#endif
}

/* The key in PKCS#8, as the server's own keys are, with its public half so commit can check it against the certificate. */
static void pkcs8(const uint8_t d[SCALAR_LEN], const uint8_t q[POINT_LEN], uint8_t out[PKCS8_LEN])
{
	static const uint8_t head[] = {0x30, 0x81, 0x87, 0x02, 0x01, 0x00, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce,
				       0x3d, 0x02, 0x01, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x04,
				       0x6d, 0x30, 0x6b, 0x02, 0x01, 0x01, 0x04, 0x20};
	static const uint8_t middle[] = {0xa1, 0x44, 0x03, 0x42, 0x00};

	BUILD_ASSERT(sizeof(head) + SCALAR_LEN + sizeof(middle) + POINT_LEN == PKCS8_LEN);
	memcpy(out, head, sizeof(head));
	memcpy(&out[sizeof(head)], d, SCALAR_LEN);
	memcpy(&out[sizeof(head) + SCALAR_LEN], middle, sizeof(middle));
	memcpy(&out[sizeof(head) + SCALAR_LEN + sizeof(middle)], q, POINT_LEN);
}

/* "tge_" and the scalar in base64url, without padding. @return 0, or -EINVAL. */
static int parse_token(const char *text, uint8_t out[SCALAR_LEN])
{
	char b64[45];
	size_t len = 0;

	if (text == NULL || strlen(text) != TOKEN_LEN || strncmp(text, TOKEN_PREFIX, 4) != 0) {
		return -EINVAL;
	}
	for (int i = 0; i < 43; i++) {
		char c = text[4 + i];

		b64[i] = c == '-' ? '+' : c == '_' ? '/' : c;
	}
	b64[43] = '=';
	b64[44] = '\0';
	if (base64_decode(out, SCALAR_LEN, &len, b64, 44) != 0 || len != SCALAR_LEN) {
		memset(out, 0, SCALAR_LEN);
		return -EINVAL;
	}
	return 0;
}

static int on_setting(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg, void *param)
{
	ARG_UNUSED(param);
	if (strcmp(key, "token") == 0 && len == SCALAR_LEN) {
		st.have_token = read_cb(cb_arg, st.token, SCALAR_LEN) == SCALAR_LEN;
	} else if (strcmp(key, "key") == 0 && len == SCALAR_LEN) {
		st.have_key = read_cb(cb_arg, st.key, SCALAR_LEN) == SCALAR_LEN;
	} else if (strcmp(key, "serial") == 0 && len < sizeof(st.serial)) {
		(void)read_cb(cb_arg, st.serial, len);
	}
	return 0;
}

/* The hardware's own ID where Zephyr can read it, or a random one, kept, where it can't. */
static void find_serial(void)
{
	uint8_t id[32];
	ssize_t n = -ENOSYS;

	if (st.serial[0] != '\0') {
		return;
	}
#if defined(CONFIG_HWINFO)
	n = hwinfo_get_device_id(id, sizeof(id));
#endif
	bool blank = true;

	for (ssize_t i = 0; i < n; i++) {
		blank = blank && (id[i] == 0 || id[i] == 0xff);
	}
	if (n < 4 || blank) {
		n = 8;
		sys_rand_get(id, (size_t)n);
		(void)bin2hex(id, (size_t)n, st.serial, sizeof(st.serial));
		(void)settings_save_one("tracegale/enroll/serial", st.serial, strlen(st.serial));
		LOG_INF("No hardware ID to enroll with, so a random serial was made: %s", st.serial);
		return;
	}
	(void)bin2hex(id, (size_t)n, st.serial, sizeof(st.serial));
}

void tg_enroll_load(void)
{
	k_mutex_lock(&lock, K_FOREVER);
	if (!st.loaded) {
		st.loaded = true;
		(void)settings_load_subtree_direct("tracegale/enroll", on_setting, NULL);
		/* A token typed into the shell wins over one built in. */
		if (!st.have_token && sizeof(CONFIG_TRACEGALE_ENROLL_TOKEN) > 1) {
			st.have_token = parse_token(CONFIG_TRACEGALE_ENROLL_TOKEN, st.token) == 0;
			if (!st.have_token) {
				LOG_ERR("CONFIG_TRACEGALE_ENROLL_TOKEN isn't an enrollment token: it starts tge_ and has 47 characters");
			}
		}
		find_serial();
	}
	k_mutex_unlock(&lock);
}

bool tg_enroll_wanted(void)
{
	return st.loaded && st.have_token && !tg_store_ready();
}

const char *tg_enroll_serial(void)
{
	return st.serial;
}

int tracegale_enroll(const char *token)
{
	uint8_t value[SCALAR_LEN];
	uint8_t q[POINT_LEN];

	if (parse_token(token, value) != 0 || keypair(value, q, false) != 0) {
		memset(value, 0, sizeof(value));
		return -TRACEGALE_ERR_BAD_ARGUMENT;
	}
	tg_enroll_load();
	k_mutex_lock(&lock, K_FOREVER);
	int rc = settings_save_one("tracegale/enroll/token", value, sizeof(value)) == 0 ? 0 : -TRACEGALE_ERR_STORAGE;

	if (rc == 0) {
		memcpy(st.token, value, sizeof(value));
		st.have_token = true;
	}
	k_mutex_unlock(&lock);
	memset(value, 0, sizeof(value));
	if (rc == 0) {
		tg_core_wake();
	}
	return rc;
}

int tracegale_set_serial(const char *serial)
{
	size_t len = serial == NULL ? 0 : strlen(serial);

	if (len == 0 || len >= sizeof(st.serial)) {
		return -TRACEGALE_ERR_BAD_ARGUMENT;
	}
	for (size_t i = 0; i < len; i++) {
		if (serial[i] < 0x21 || serial[i] > 0x7e) {
			return -TRACEGALE_ERR_BAD_ARGUMENT;
		}
	}
	tg_enroll_load();
	k_mutex_lock(&lock, K_FOREVER);
	int rc = settings_save_one("tracegale/enroll/serial", serial, len) == 0 ? 0 : -TRACEGALE_ERR_STORAGE;

	if (rc == 0) {
		memset(st.serial, 0, sizeof(st.serial));
		memcpy(st.serial, serial, len);
	}
	k_mutex_unlock(&lock);
	return rc;
}

void tg_enroll_forget(void)
{
	k_mutex_lock(&lock, K_FOREVER);
	(void)settings_delete("tracegale/enroll/token");
	(void)settings_delete("tracegale/enroll/key");
	memset(st.key, 0, sizeof(st.key));
	st.have_key = false;
	/* A built-in token stays, so the device enrolls again, with a new key, which waits for approval. */
	st.have_token = sizeof(CONFIG_TRACEGALE_ENROLL_TOKEN) > 1 && parse_token(CONFIG_TRACEGALE_ENROLL_TOKEN, st.token) == 0;
	k_mutex_unlock(&lock);
}

/* The device's answer to ENROLLED: check the certificate is for its own key and names the ID, then provision with it. */
static int use_certificate(const struct tg_message *reply, const uint8_t q[POINT_LEN], int *detail)
{
	const char *id;
	size_t id_len, cert_len, key_len;
	const uint8_t *cert, *cert_key;
	char named[TG_IDENTITY_MAX + 1];
	uint8_t key[PKCS8_LEN];

	if (!tg_read_text(reply, 0, &id, &id_len) || !tg_read_bytes(reply, 1, &cert, &cert_len) || cert_len > CONFIG_TRACEGALE_CRED_MAX_SIZE) {
		*detail = 1;
		return -TRACEGALE_ERR_BAD_REPLY;
	}
	if (tg_der_cert_public_key(cert, cert_len, &cert_key, &key_len) != 0 || key_len != POINT_LEN || memcmp(cert_key, q, POINT_LEN) != 0) {
		LOG_ERR("The server's certificate isn't for this device's key");
		*detail = 2;
		return -TRACEGALE_ERR_BAD_REPLY;
	}
	if (tg_identity_from_cert(cert, cert_len, named, sizeof(named)) != 0 || strlen(named) != id_len || memcmp(named, id, id_len) != 0) {
		*detail = 3;
		return -TRACEGALE_ERR_BAD_REPLY;
	}
	pkcs8(st.key, q, key);
	int rc = tracegale_provision_server(CONFIG_TRACEGALE_SERVER_HOST, 0);

	if (rc == 0) {
		rc = tg_store_pending_b64(TRACEGALE_CREDENTIAL_CA, CONFIG_TRACEGALE_SERVER_CA);
	}
	if (rc == 0) {
		rc = tracegale_provision_credential(TRACEGALE_CREDENTIAL_DEVICE_CERT, cert, cert_len);
	}
	if (rc == 0) {
		rc = tracegale_provision_credential(TRACEGALE_CREDENTIAL_DEVICE_KEY, key, sizeof(key));
	}
	memset(key, 0, sizeof(key));
	if (rc == 0) {
		rc = tracegale_provision_commit();
	}
	if (rc != 0) {
		*detail = 4;
		return rc;
	}
	LOG_INF("Enrolled as %s, serial %s", named, st.serial);
	/* The key is now stored as the device's credential, and the token isn't needed any more. */
	(void)settings_delete("tracegale/enroll/token");
	(void)settings_delete("tracegale/enroll/key");
	return 0;
}

int tg_enroll_run(uint8_t *buf, size_t cap, uint32_t id, uint32_t *retry_s, uint32_t *server_error, int *detail)
{
	const struct tg_transport *transport = tg_transport_get();
	uint8_t claim_key[PKCS8_LEN], q[POINT_LEN], scalar[SCALAR_LEN];
	size_t reply_len = 0;
	struct tg_message reply;
	struct tg_writer w;
	int rc;

	k_mutex_lock(&lock, K_FOREVER);
	if (!st.have_token) {
		k_mutex_unlock(&lock);
		return -TRACEGALE_ERR_NOT_PROVISIONED;
	}
	/* The claim certificate's key, from the token. */
	memcpy(scalar, st.token, SCALAR_LEN);
	rc = keypair(scalar, q, false);
	if (rc == 0) {
		pkcs8(scalar, q, claim_key);
	}
	/* The device's own key, made once and kept, so asking again after a lost reply, or a restart, is the same request. */
	if (rc == 0 && !st.have_key) {
		rc = keypair(st.key, q, true);
		if (rc == 0) {
			rc = settings_save_one("tracegale/enroll/key", st.key, SCALAR_LEN);
			st.have_key = rc == 0;
		}
	}
	if (rc == 0) {
		rc = keypair(st.key, q, false);
	}
	memset(scalar, 0, sizeof(scalar));
	if (rc != 0) {
		k_mutex_unlock(&lock);
		memset(claim_key, 0, sizeof(claim_key));
		LOG_ERR("Can't make or keep the device's key pair: %d", rc);
		*detail = rc;
		return -TRACEGALE_ERR_STORAGE;
	}
	rc = tg_store_use_claim(CONFIG_TRACEGALE_SERVER_HOST, CONFIG_TRACEGALE_SERVER_PORT, CONFIG_TRACEGALE_SERVER_CA,
				CONFIG_TRACEGALE_ENROLL_CERT, claim_key, sizeof(claim_key));
	memset(claim_key, 0, sizeof(claim_key));
	if (rc == -TRACEGALE_ERR_BAD_ARGUMENT) {
		LOG_ERR("CONFIG_TRACEGALE_SERVER_CA or CONFIG_TRACEGALE_ENROLL_CERT isn't valid: use the build file from the web app");
	}
	if (rc == 0) {
		rc = transport->open(detail);
		if (rc == 0) {
			tg_write_begin(&w, buf, cap, TG_ENROLL, id, 3);
			tg_write_text(&w, 0, st.serial);
			tg_write_text(&w, 1, CONFIG_TRACEGALE_HARDWARE);
			tg_write_bytes(&w, 2, q, POINT_LEN);
			rc = w.overflow ? -TRACEGALE_ERR_TOO_LARGE : transport->exchange(buf, w.len, buf, cap, &reply_len, detail);
			transport->close();
		}
	}
	if (rc == 0 && (tg_read_message(buf, reply_len, &reply) != 0 || reply.id != id)) {
		rc = -TRACEGALE_ERR_BAD_REPLY;
	}
	if (rc == 0 && reply.type == TG_ERROR) {
		uint32_t code = 0, retry = 0;

		(void)tg_read_uint(&reply, 0, &code);
		(void)tg_read_uint(&reply, 1, &retry);
		*server_error = code;
		*retry_s = retry;
		*detail = (int)code;
		rc = code == ERR_PENDING_APPROVAL ? -TRACEGALE_ERR_WAITING_FOR_APPROVAL : -TRACEGALE_ERR_SERVER;
	} else if (rc == 0 && reply.type != TG_ENROLLED) {
		rc = -TRACEGALE_ERR_BAD_REPLY;
	}
	tg_store_drop_claim();
	if (rc == 0) {
		rc = use_certificate(&reply, q, detail);
	}
	k_mutex_unlock(&lock);
	return rc;
}
