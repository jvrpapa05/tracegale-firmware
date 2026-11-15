/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * The socket calls the MQTT binding makes, served from the fuzzer's input by fuzz_mqtt.c.
 */

#ifndef TG_FUZZ_SHIM_SOCKET_H_
#define TG_FUZZ_SHIM_SOCKET_H_

#include <stddef.h>
#include <sys/types.h>

#define ZSOCK_POLLIN 1

struct zsock_pollfd {
	int fd;
	short events;
	short revents;
};

int zsock_poll(struct zsock_pollfd *fds, int nfds, int timeout);
ssize_t zsock_recv(int sock, void *buf, size_t len, int flags);
ssize_t zsock_send(int sock, const void *buf, size_t len, int flags);
int zsock_close(int sock);

#endif /* TG_FUZZ_SHIM_SOCKET_H_ */
