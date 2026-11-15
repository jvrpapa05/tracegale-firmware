/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * Differences between the supported Zephyr versions, kept in one place.
 */

#ifndef TG_COMPAT_H_
#define TG_COMPAT_H_

#include <stdint.h>
#include <stdio.h>
#include <zephyr/version.h>

#if KERNEL_VERSION_MAJOR < 4
#define TG_AF_UNSPEC AF_UNSPEC
#define TG_SOCK_STREAM SOCK_STREAM
#define TG_PROTO_TLS IPPROTO_TLS_1_2
#define TG_PROTO_TCP IPPROTO_TCP
#define TG_SOL_TLS SOL_TLS
#define TG_TLS_SEC_TAG_LIST TLS_SEC_TAG_LIST
#define TG_TLS_HOSTNAME TLS_HOSTNAME
#define TG_TLS_PEER_VERIFY TLS_PEER_VERIFY
#define TG_TLS_PEER_VERIFY_REQUIRED TLS_PEER_VERIFY_REQUIRED
#define TG_TCP_NODELAY TCP_NODELAY
#define TG_TLS_DEVICE_CERT TLS_CREDENTIAL_SERVER_CERTIFICATE
#define tg_sockaddr_in sockaddr_in
#define tg_sockaddr_in6 sockaddr_in6
#define TG_AF_INET AF_INET
#define tg_htons htons
typedef uint32_t tg_net_event_t;
#else
typedef uint64_t tg_net_event_t;
#define TG_AF_UNSPEC NET_AF_UNSPEC
#define TG_SOCK_STREAM NET_SOCK_STREAM
#define TG_PROTO_TLS NET_IPPROTO_TLS_1_2
#define TG_PROTO_TCP NET_IPPROTO_TCP
#define TG_SOL_TLS ZSOCK_SOL_TLS
#define TG_TLS_SEC_TAG_LIST ZSOCK_TLS_SEC_TAG_LIST
#define TG_TLS_HOSTNAME ZSOCK_TLS_HOSTNAME
#define TG_TLS_PEER_VERIFY ZSOCK_TLS_PEER_VERIFY
#define TG_TLS_PEER_VERIFY_REQUIRED ZSOCK_TLS_PEER_VERIFY_REQUIRED
#define TG_TCP_NODELAY ZSOCK_TCP_NODELAY
#define TG_TLS_DEVICE_CERT TLS_CREDENTIAL_PUBLIC_CERTIFICATE
#define tg_sockaddr_in net_sockaddr_in
#define tg_sockaddr_in6 net_sockaddr_in6
#define TG_AF_INET NET_AF_INET
#define tg_htons net_htons
#endif

/* Zephyr v4.4 renamed the macro that gives a flash partition's ID, and the old name now warns. */
#if KERNEL_VERSION_NUMBER >= ZEPHYR_VERSION(4, 4, 0)
#define TG_PARTITION_ID(label) PARTITION_ID(label)
#else
#define TG_PARTITION_ID(label) FIXED_PARTITION_ID(label)
#endif

#endif /* TG_COMPAT_H_ */
