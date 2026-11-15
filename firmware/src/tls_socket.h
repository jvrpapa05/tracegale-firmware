/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * A TLS connection to the provisioned server, which every binding uses.
 */

#ifndef TG_TLS_SOCKET_H_
#define TG_TLS_SOCKET_H_

/**
 * Connects to the provisioned server and port over TLS, with the device's credentials, always verifying the server.
 *
 * @param out gets the socket.
 * @return 0, or a negative tracegale_error, with *detail set if useful.
 */
int tg_tls_open(int *out, int *detail);

#endif /* TG_TLS_SOCKET_H_ */
