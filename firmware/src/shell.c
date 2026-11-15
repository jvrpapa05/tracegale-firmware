/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 *
 * Shows state and provisions a device. Credentials can be written here but are never printed.
 */

#include <stdlib.h>
#include <string.h>
#include <zephyr/shell/shell.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/base64.h>
#include <zephyr/sys/util.h>

#include <tracegale/tracegale.h>
#include <tracegale/version.h>

#include "build_id.h"
#include "core.h"
#if defined(CONFIG_TRACEGALE_METRICS)
#include <zephyr/sys/iterable_sections.h>
#include "metrics.h"
#endif
#if defined(CONFIG_TRACEGALE_LOGS)
#include "logs.h"
#endif
#include "store.h"

static const char *const state_names[] = {"unprovisioned", "connecting", "idle", "backing off"};

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	struct tracegale_status s;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	tracegale_get_status(&s);
	shell_print(sh, "firmware: %s", tg_core_fw_version());
	shell_print(sh, "tracegale: %s", tracegale_version());
	shell_print(sh, "state: %s", state_names[s.state]);
	shell_print(sh, "server: %s:%u", tg_store_ready() ? tg_store_host() : "none", tg_store_port());
	shell_print(sh, "attempts: %u", s.attempts);
	shell_print(sh, "successes: %u", s.successes);
	shell_print(sh, "last error: %d, detail %d", s.last_error, s.last_error_detail);
	shell_print(sh, "last server error: %u", s.last_server_error);
	shell_print(sh, "last message: %u", s.last_message_id);
	shell_print(sh, "next attempt: %u s", s.next_attempt_s);
	shell_print(sh, "stack free: %u", s.stack_free);
	shell_print(sh, "reset reason: %u", s.reset_reason);

	const uint8_t *id;
	size_t len = tg_build_id(&id);
	char hex[2 * TG_BUILD_ID_MAX + 1] = "none";

	if (len > 0) {
		(void)bin2hex(id, len, hex, sizeof(hex));
	}
#if defined(CONFIG_TRACEGALE_CRASH)
	shell_print(sh, "crash waiting: %u bytes", s.crash_pending);
	shell_print(sh, "crashes sent: %u", s.crashes_sent);
#endif
#if defined(CONFIG_TRACEGALE_OTA)
	static const char *const update_names[] = {"none", "downloading", "restart pending", "testing", "reporting"};

	shell_print(sh, "update: %s, %u of %u bytes", update_names[s.update_state], s.update_offset, s.update_total);
	shell_print(sh, "last update result: %u", s.update_result);
#endif
#if defined(CONFIG_TRACEGALE_METRICS)
	shell_print(sh, "metric reports sent: %u", s.metrics_sent);
#endif
#if defined(CONFIG_TRACEGALE_LOGS)
	shell_print(sh, "log records sent: %u, waiting: %u, dropped: %u", s.logs_sent, s.logs_waiting, s.logs_dropped);
#endif
	/* Always the last line, which tests rely on. */
	shell_print(sh, "build id: %s", hex);
	return 0;
}

#if defined(CONFIG_TRACEGALE_LOGS)
static int cmd_logs(const struct shell *sh, size_t argc, char **argv)
{
	static const char level_names[] = "?EWID";
	struct tg_logs_state state;
	struct tg_logs_record record;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	tg_logs_state_get(&state);
	shell_print(sh, "%u records waiting, %zu of %u bytes, %u dropped", state.waiting, state.used,
		    CONFIG_TRACEGALE_LOGS_BUFFER_SIZE, state.dropped);
	for (uint32_t i = 0; tg_logs_peek(i, &record); i++) {
		shell_print(sh, "%u %llu ms %c %s: %s", record.seq, (unsigned long long)record.uptime_ms,
			    level_names[MIN(record.level, 4)], record.module, record.text);
	}
	return 0;
}
#endif

#if defined(CONFIG_TRACEGALE_METRICS)
/* What the next report would hold. Looking doesn't change it. */
static int cmd_metrics(const struct shell *sh, size_t argc, char **argv)
{
	static const char *const names[TG_SYS_COUNT + 1] = {
		[TG_SYS_UPTIME_S] = "uptime_s",
		[TG_SYS_HEAP_FREE] = "heap_free",
		[TG_SYS_HEAP_LOW] = "heap_low",
		[TG_SYS_STACK_LOW] = "stack_low",
		[TG_SYS_CPU_LOAD] = "cpu_load",
		[TG_SYS_NET_SENT] = "net_sent",
		[TG_SYS_NET_RECEIVED] = "net_received",
		[TG_SYS_CHECKIN_FAILURES] = "checkin_failures",
		[TG_SYS_CHECKIN_MS] = "checkin_ms",
	};
	struct tg_metrics_system system;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	tg_metrics_system_get(&system);
	for (size_t i = 0; i < system.count; i++) {
		shell_print(sh, "system %s: %u", names[system.key[i]], system.value[i]);
	}
	STRUCT_SECTION_FOREACH(tracegale_metric, metric) {
		shell_print(sh, "%s %s: %d", metric->kind == TRACEGALE_METRIC_COUNTER ? "counter" : "gauge", metric->name,
			    metric->value);
	}
	return 0;
}
#endif

static int cmd_server(const struct shell *sh, size_t argc, char **argv)
{
	long port = strtol(argv[2], NULL, 10);
	int rc;

	ARG_UNUSED(argc);
	rc = (port > 0 && port <= UINT16_MAX) ? tracegale_provision_server(argv[1], port) : -TRACEGALE_ERR_BAD_ARGUMENT;
	if (rc != 0) {
		shell_error(sh, "error %d", -rc);
	}
	return rc;
}

static int cmd_cred(const struct shell *sh, size_t argc, char **argv)
{
	static const char *const names[TRACEGALE_CREDENTIAL_COUNT] = {"ca", "cert", "key"};
	uint8_t decoded[192];
	size_t len = 0;
	int type = -1;

	ARG_UNUSED(argc);
	for (int i = 0; i < TRACEGALE_CREDENTIAL_COUNT; i++) {
		if (strcmp(argv[1], names[i]) == 0) {
			type = i;
		}
	}
	if (type < 0) {
		shell_error(sh, "unknown credential %s: use ca, cert or key", argv[1]);
		return -EINVAL;
	}
	if (strcmp(argv[2], "clear") == 0) {
		tg_store_clear_pending(type);
		return 0;
	}
	if (base64_decode(decoded, sizeof(decoded), &len, argv[2], strlen(argv[2])) != 0) {
		shell_error(sh, "not valid base64, or a piece over %u bytes", (unsigned int)sizeof(decoded));
		return -EINVAL;
	}
	int rc = tg_store_append(type, decoded, len);

	/* The buffer held key material. */
	memset(decoded, 0, sizeof(decoded));
	if (rc != 0) {
		shell_error(sh, "error %d: credential too large", -rc);
		return rc;
	}
	shell_print(sh, "%s: %u bytes", names[type], (unsigned int)tg_store_pending_len(type));
	return 0;
}

static int cmd_commit(const struct shell *sh, size_t argc, char **argv)
{
	int rc = tracegale_provision_commit();

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	if (rc != 0) {
		shell_error(sh, "error %d", -rc);
		return rc;
	}
	shell_print(sh, "provisioned");
	return 0;
}

static int cmd_erase(const struct shell *sh, size_t argc, char **argv)
{
	int rc = tracegale_provision_erase();

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	if (rc != 0) {
		shell_error(sh, "error %d", -rc);
		return rc;
	}
	shell_print(sh, "erased");
	return 0;
}

static int cmd_connect(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	tracegale_connect_now();
	return 0;
}

#if defined(CONFIG_TRACEGALE_SHELL_TEST)
/* Kept out of line, so that a backtrace shows these frames. */
static __noinline void test_crash_deepest(int how)
{
	if (how == 0) {
		k_oops();
	}
	/* An undefined instruction: a real CPU fault, rather than the kernel's own oops. */
	__builtin_trap();
}

static __noinline void test_crash_middle(int how)
{
	test_crash_deepest(how);
	/* Stops a tail call, which would leave this frame out. */
	__asm__ volatile("" ::: "memory");
}

static int cmd_test_crash(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	shell_print(sh, "crashing with k_oops()");
	test_crash_middle(0);
	return 0;
}

static int cmd_test_fault(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	shell_print(sh, "crashing with an undefined instruction");
	test_crash_middle(1);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(tracegale_test_cmds,
	SHELL_CMD_ARG(crash, NULL, "Crash with k_oops(), to test crash reports", cmd_test_crash, 1, 0),
	SHELL_CMD_ARG(fault, NULL, "Crash with a CPU fault, to test crash reports", cmd_test_fault, 1, 0),
	SHELL_SUBCMD_SET_END);
#define TEST_CMDS (&tracegale_test_cmds)
#else
/* Zephyr's conditional command still names its subcommands when it's off. */
#define TEST_CMDS NULL
#endif

SHELL_STATIC_SUBCMD_SET_CREATE(tracegale_cmds,
	SHELL_CMD_ARG(status, NULL, "Show state", cmd_status, 1, 0),
	SHELL_CMD_ARG(server, NULL, "Set the server: <host> <port>", cmd_server, 3, 0),
	SHELL_CMD_ARG(cred, NULL, "Add a piece of a credential, and check the total it reports: <ca|cert|key> <base64|clear>",
		      cmd_cred, 3, 0),
	SHELL_CMD_ARG(commit, NULL, "Use and store what was provisioned", cmd_commit, 1, 0),
	SHELL_CMD_ARG(erase, NULL, "Forget the server and credentials", cmd_erase, 1, 0),
	SHELL_CMD_ARG(connect, NULL, "Attempt now", cmd_connect, 1, 0),
#if defined(CONFIG_TRACEGALE_LOGS)
	SHELL_CMD_ARG(logs, NULL, "Show the log records waiting to be sent", cmd_logs, 1, 0),
#endif
#if defined(CONFIG_TRACEGALE_METRICS)
	SHELL_CMD_ARG(metrics, NULL, "Show what the next metric report would hold", cmd_metrics, 1, 0),
#endif
	SHELL_COND_CMD(CONFIG_TRACEGALE_SHELL_TEST, test, TEST_CMDS, "Crash on purpose", NULL),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(tracegale, &tracegale_cmds, "Tracegale fleet firmware stack", NULL);
