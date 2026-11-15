/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>

#include <tracegale/tracegale.h>

#include "compat.h"
#include "store.h"
#include "tls_socket.h"

LOG_MODULE_DECLARE(tracegale, CONFIG_TRACEGALE_LOG_LEVEL);

/* Tries one address. @return 0, or a negative tracegale_error. */
static int connect_to(int *out, const struct zsock_addrinfo *addr, const char *host, int *detail)
{
	static const sec_tag_t tags[] = {CONFIG_TRACEGALE_SEC_TAG};
	int verify = TG_TLS_PEER_VERIFY_REQUIRED;
	int on = 1;
	int sock = zsock_socket(addr->ai_family, TG_SOCK_STREAM, TG_PROTO_TLS);

	if (sock < 0) {
		*detail = errno;
		return -TRACEGALE_ERR_CONNECT;
	}
	/* The server's certificate and name are always verified. There is no way to turn this off. */
	if (zsock_setsockopt(sock, TG_SOL_TLS, TG_TLS_SEC_TAG_LIST, tags, sizeof(tags)) != 0 ||
	    zsock_setsockopt(sock, TG_SOL_TLS, TG_TLS_PEER_VERIFY, &verify, sizeof(verify)) != 0 ||
	    zsock_setsockopt(sock, TG_SOL_TLS, TG_TLS_HOSTNAME, host, strlen(host) + 1) != 0) {
		*detail = errno;
		LOG_ERR("Can't set up TLS: %d", *detail);
		zsock_close(sock);
		return -TRACEGALE_ERR_TLS;
	}
	/* Plan 02 measured each exchange getting faster with TCP batching off. It isn't required, so a failure doesn't matter. */
	(void)zsock_setsockopt(sock, TG_PROTO_TCP, TG_TCP_NODELAY, &on, sizeof(on));
	if (zsock_connect(sock, addr->ai_addr, addr->ai_addrlen) == 0) {
		*out = sock;
		return 0;
	}
	*detail = errno;
	LOG_WRN("Can't connect to %s: %d", host, *detail);
	zsock_close(sock);
	/* Zephyr's TLS layer reports a failed handshake, including a certificate it refused, as ECONNABORTED on connect. */
	return *detail == ECONNABORTED ? -TRACEGALE_ERR_TLS : -TRACEGALE_ERR_CONNECT;
}

int tg_tls_open(int *out, int *detail)
{
	struct zsock_addrinfo hints = {.ai_family = TG_AF_UNSPEC, .ai_socktype = TG_SOCK_STREAM};
	struct zsock_addrinfo *found = NULL;
	char port[6];
	const char *host = tg_store_host();
	int rc;

	*out = -1;
	snprintf(port, sizeof(port), "%u", tg_store_port());
	rc = zsock_getaddrinfo(host, port, &hints, &found);
	if (rc != 0) {
		*detail = rc;
		return -TRACEGALE_ERR_DNS;
	}
	/*
	 * A name can have both IPv4 and IPv6 addresses, and only some may work from here. Every address gets tried, because Zephyr
	 * v3.7 can report a refused connection the same way as a failed handshake. A TLS failure is the more useful one to report.
	 */
	rc = -TRACEGALE_ERR_CONNECT;
	for (const struct zsock_addrinfo *addr = found; addr != NULL; addr = addr->ai_next) {
		int attempt_detail = 0;
		int attempt = connect_to(out, addr, host, &attempt_detail);

		if (attempt == 0) {
			rc = 0;
			break;
		}
		if (rc != -TRACEGALE_ERR_TLS) {
			rc = attempt;
			*detail = attempt_detail;
		}
	}
	zsock_freeaddrinfo(found);
	return rc;
}
