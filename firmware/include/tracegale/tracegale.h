/*
 * SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TRACEGALE_TRACEGALE_H_
#define TRACEGALE_TRACEGALE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/kernel.h>

#if defined(CONFIG_TRACEGALE_METRICS)
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/toolchain.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Failures, documented in docs/firmware/errors.md. Each value is stable. */
enum tracegale_error {
	TRACEGALE_OK = 0,
	TRACEGALE_ERR_NOT_PROVISIONED = 1,
	TRACEGALE_ERR_DNS = 2,
	TRACEGALE_ERR_CONNECT = 3,
	TRACEGALE_ERR_TLS = 4,
	TRACEGALE_ERR_SEND = 5,
	TRACEGALE_ERR_TIMEOUT = 6,
	TRACEGALE_ERR_HTTP_STATUS = 7,
	TRACEGALE_ERR_BAD_REPLY = 8,
	TRACEGALE_ERR_SERVER = 9,
	TRACEGALE_ERR_TOO_LARGE = 10,
	TRACEGALE_ERR_STORAGE = 11,
	TRACEGALE_ERR_BAD_ARGUMENT = 12,
	/** The MQTT broker refused the connection. The detail is its CONNACK return code, such as 5, not authorized. */
	TRACEGALE_ERR_REFUSED = 13,
	/** The device certificate names no identity Tracegale can use. */
	TRACEGALE_ERR_IDENTITY = 14,
	/** Enrolled, and waiting for someone to approve the device in the web app. It asks again by itself. */
	TRACEGALE_ERR_WAITING_FOR_APPROVAL = 15,
};

enum tracegale_state {
	/** Waiting for a server address and credentials. */
	TRACEGALE_STATE_UNPROVISIONED,
	/** Talking to the server now. */
	TRACEGALE_STATE_CONNECTING,
	/** The last attempt worked, and the next check-in is scheduled. */
	TRACEGALE_STATE_IDLE,
	/** The last attempt failed, and the next attempt is scheduled. */
	TRACEGALE_STATE_BACKING_OFF,
	/** The application said its link is down, with tracegale_link_set(false). Nothing is attempted until it's back. */
	TRACEGALE_STATE_WAITING_FOR_LINK,
};

enum tracegale_credential {
	/** CA certificate that the server's certificate must chain to, in DER form. */
	TRACEGALE_CREDENTIAL_CA,
	/** This device's certificate, in DER form. */
	TRACEGALE_CREDENTIAL_DEVICE_CERT,
	/** This device's private key, in DER form. */
	TRACEGALE_CREDENTIAL_DEVICE_KEY,
	TRACEGALE_CREDENTIAL_COUNT,
};

/** What an update is doing. */
enum tracegale_update_state {
	/** No update is in progress. */
	TRACEGALE_UPDATE_NONE = 0,
	/** Part of a new image is in the second slot. The download continues when the server offers it again. */
	TRACEGALE_UPDATE_DOWNLOADING = 1,
	/** A new image is downloaded, and is installed at the next restart. */
	TRACEGALE_UPDATE_RESTART_PENDING = 2,
	/** A new image is running on trial. The bootloader brings the previous one back unless it's confirmed. */
	TRACEGALE_UPDATE_TESTING = 3,
	/** An update ended, and the server hasn't been told yet. */
	TRACEGALE_UPDATE_REPORTING = 4,
};

/** How an update ended. These are the IMG_RESULT outcomes in docs/protocol/README.md, and each value is stable. */
enum tracegale_update_result {
	TRACEGALE_UPDATE_NO_RESULT = 0,
	/** The new image runs and is confirmed. */
	TRACEGALE_UPDATE_INSTALLED = 1,
	/** The bootloader didn't start the new image, for example because its signature is wrong. */
	TRACEGALE_UPDATE_NOT_STARTED = 2,
	/** The new image started but wasn't confirmed, so the bootloader brought the previous image back. */
	TRACEGALE_UPDATE_REVERTED = 3,
	/** What was downloaded doesn't have the SHA-256 that was offered. */
	TRACEGALE_UPDATE_DAMAGED = 4,
	/** The image doesn't fit the second slot. */
	TRACEGALE_UPDATE_TOO_LARGE = 5,
	/** It doesn't start with an MCUboot image header. */
	TRACEGALE_UPDATE_NOT_AN_IMAGE = 6,
	/** It's older than the running image, and CONFIG_TRACEGALE_OTA_DOWNGRADE is off. */
	TRACEGALE_UPDATE_DOWNGRADE = 7,
	/** Flash or settings storage failed. */
	TRACEGALE_UPDATE_STORAGE = 8,
	/** The offer wasn't for a full image, which is all this firmware can install. */
	TRACEGALE_UPDATE_UNSUPPORTED_KIND = 9,
};

/** Why Tracegale wants the device restarted. */
enum tracegale_restart_reason {
	/** A new image is downloaded, and the bootloader installs it at the restart. */
	TRACEGALE_RESTART_INSTALL,
	/** A new image wasn't confirmed in time, and the bootloader brings the previous one back at the restart. */
	TRACEGALE_RESTART_REVERT,
};

struct tracegale_status {
	enum tracegale_state state;
	/** The most recent failure, which stays set after a later success, for debugging. */
	enum tracegale_error last_error;
	/** The system's error number behind last_error, if any. */
	int last_error_detail;
	/** The server's error code from its last ERROR message. See docs/protocol/README.md. */
	uint32_t last_server_error;
	/** ID of the last check-in, crash upload or update message, which also appears in the server's records. Reports of metrics
	 *  and logs, which go with every check-in, keep the check-in's ID, and their own IDs are in the server's log.
	 */
	uint32_t last_message_id;
	uint32_t attempts;
	uint32_t successes;
	/** Seconds until the next attempt. */
	uint32_t next_attempt_s;
	/** The reset reason reported to the server. See docs/protocol/README.md. */
	uint32_t reset_reason;
	/** Least free stack the Tracegale thread has had, in bytes. Needs CONFIG_INIT_STACKS, or it reads 0. */
	uint32_t stack_free;
	/** Size of a stored crash not yet sent, in bytes, or 0. */
	uint32_t crash_pending;
	/** Crashes sent since boot. */
	uint32_t crashes_sent;
	/** What an update is doing. Always TRACEGALE_UPDATE_NONE without CONFIG_TRACEGALE_OTA. */
	enum tracegale_update_state update_state;
	/** How the last update ended, or TRACEGALE_UPDATE_NO_RESULT. Kept through restarts. */
	enum tracegale_update_result update_result;
	/** Bytes of the new image downloaded so far, and its size. Both 0 when no image is being downloaded. */
	uint32_t update_offset;
	uint32_t update_total;
	/** Metric reports the server has acknowledged since boot. Always 0 without CONFIG_TRACEGALE_METRICS. */
	uint32_t metrics_sent;
	/** Log records the server has acknowledged since boot, and records waiting to be sent. 0 without CONFIG_TRACEGALE_LOGS. */
	uint32_t logs_sent;
	uint32_t logs_waiting;
	/** Log records lost because the buffer was full, and not yet reported to the server. */
	uint32_t logs_dropped;
};

/**
 * @brief Set the server to connect to. Takes effect at tracegale_provision_commit().
 *
 * @param host Name in the server's certificate, which is always verified.
 * @param port TCP port, or 0 for CONFIG_TRACEGALE_SERVER_PORT: 8443 for HTTPS and 8883 for MQTT unless set otherwise.
 * @return 0, or a negative tracegale_error.
 */
int tracegale_provision_server(const char *host, uint16_t port);

/**
 * @brief Set one credential, replacing any earlier value. Takes effect at tracegale_provision_commit().
 *
 * @return 0, or a negative tracegale_error.
 */
int tracegale_provision_credential(enum tracegale_credential type, const uint8_t *der, size_t len);

/**
 * @brief Use the provisioned values, and store them if the application enables CONFIG_SETTINGS.
 *
 * Without CONFIG_SETTINGS, values last until the next reset. With it, they are stored in plain flash unless the application
 * has encrypted that flash, including the private key. See docs/firmware/README.md.
 *
 * Credentials set since the last commit are checked first: each must be one whole DER element, and the key must belong to
 * the device certificate. A piece entered twice or left out is refused here, with a logged reason, and nothing changes.
 *
 * @return 0, or a negative tracegale_error: TRACEGALE_ERR_BAD_ARGUMENT for credentials that fail those checks.
 */
int tracegale_provision_commit(void);

/**
 * @brief Forget the server and credentials, including any stored copies.
 *
 * @return 0, or a negative tracegale_error.
 */
int tracegale_provision_erase(void);

/**
 * @brief Give the device an enrollment token, so it enrolls by itself. Needs CONFIG_TRACEGALE_ENROLL and the build file of
 * the token's enrollment key.
 *
 * For devices whose firmware image mustn't hold the token: give it once, at the factory or on the bench, and it's kept in
 * settings storage until the device has its own certificate, then deleted. The shell has it as tracegale enroll <token>.
 *
 * @param token "tge_" and 43 characters, as the web app showed it when the key was made.
 * @return 0, or a negative tracegale_error: TRACEGALE_ERR_BAD_ARGUMENT for something that isn't a token.
 */
int tracegale_enroll(const char *token);

/**
 * @brief Set the serial number the device enrolls with, such as the one on its label, in place of the chip's ID.
 *
 * Kept in settings storage. Call it before the device enrolls: a device that has enrolled keeps the identity it has. The
 * server tells devices apart by serial within a project, so each must be different.
 *
 * @param serial 1 to 64 printable ASCII characters, without spaces.
 * @return 0, or a negative tracegale_error.
 */
int tracegale_set_serial(const char *serial);

/** @brief Ask for an attempt now, instead of waiting for the next one. It doesn't wait: see tracegale_check_in(). */
void tracegale_connect_now(void);

/**
 * @brief Make one attempt now, and wait until it has finished.
 *
 * For a link the application turns on only for a moment, such as a modem it powers for a check-in: call this while the link
 * is up, and turn the link off when it returns. The attempt is one that starts after the call, whatever else is under way, and
 * it's made even while tracegale_link_set(false) holds the schedule back. It sends what's waiting, as a scheduled
 * check-in does.
 *
 * With CONFIG_TRACEGALE_AUTOSTART, Tracegale's thread makes the attempt, and this waits for it up to @p timeout. Without it,
 * the attempt runs on the calling thread and @p timeout isn't used: each message is bounded by CONFIG_TRACEGALE_TIMEOUT_MS.
 * Never call it from Tracegale's own thread, such as from tracegale_update_restart().
 *
 * @return 0 if the attempt worked, -TRACEGALE_ERR_TIMEOUT if it hadn't finished in time, or the attempt's negative
 *         tracegale_error, as in tracegale_status.last_error.
 */
int tracegale_check_in(k_timeout_t timeout);

/**
 * @brief Say whether the application's link to the server is up. Up at boot unless CONFIG_TRACEGALE_LINK_UP_AT_BOOT=n.
 *
 * While it's down, Tracegale makes no scheduled attempts, so none fail and none count in checkin_failures or the backoff,
 * and its state is TRACEGALE_STATE_WAITING_FOR_LINK. When it's up again, an attempt that came due meanwhile is made at once.
 * tracegale_check_in() and tracegale_connect_now() still make an attempt while it's down.
 *
 * Use it for a link the application powers or shares, such as a cellular modem. Zephyr's connection manager isn't needed.
 */
void tracegale_link_set(bool up);

/** @brief Get the current state, for debugging or display. */
void tracegale_get_status(struct tracegale_status *status);

/**
 * @brief Stop, or allow again, the download of updates. Allowed at boot.
 *
 * While paused, Tracegale neither erases the second slot nor downloads into it, and a download that was under way stops
 * within one message. It continues from the same place once allowed again. Use it while the device does something that an
 * update mustn't slow down or interrupt. It doesn't hold back a restart: for that, provide tracegale_update_restart().
 *
 * Does nothing without CONFIG_TRACEGALE_OTA.
 */
void tracegale_update_pause(bool pause);

/**
 * @brief Called when Tracegale wants the device restarted for an update. Applications can provide their own.
 *
 * Tracegale's own version restarts the device at once. An application that must finish something first provides this
 * function, and restarts when it's ready, with sys_reboot(). If it returns without restarting:
 *
 * - for TRACEGALE_RESTART_INSTALL, it's called again after each check-in, and the new image is installed at the next
 *   restart, whatever causes it;
 * - for TRACEGALE_RESTART_REVERT, it isn't called again, and the previous image comes back at the next restart.
 *
 * It's called on Tracegale's thread to install, and on the system work queue to revert, so it mustn't block for long.
 */
void tracegale_update_restart(enum tracegale_restart_reason reason);

/** How a metric is reported. */
enum tracegale_metric_kind {
	/** The latest reading, such as a battery voltage. Reported for each period in which it was set. */
	TRACEGALE_METRIC_GAUGE = 0,
	/** A total for the period, such as the times a door opened. It starts again from zero once it's reported. */
	TRACEGALE_METRIC_COUNTER = 1,
};

/** One of the application's own metrics. Made with TRACEGALE_METRIC_GAUGE_DEFINE() or _COUNTER_DEFINE() only. */
struct tracegale_metric {
	/** @cond INTERNAL_HIDDEN */
	const char *name;
	int32_t value;
	int32_t sent;
	uint8_t kind;
	uint8_t flags;
	/** @endcond */
};

#if defined(CONFIG_TRACEGALE_METRICS)

/** @cond INTERNAL_HIDDEN */
#define Z_TRACEGALE_METRIC_DEFINE(_name, _kind)                                                                              \
	BUILD_ASSERT(sizeof(#_name) > 1 && sizeof(#_name) <= 33, "A metric's name needs 1 to 32 bytes");                    \
	STRUCT_SECTION_ITERABLE(tracegale_metric, tracegale_metric_##_name) = {.name = #_name, .kind = (_kind)}
/** @endcond */

/**
 * @brief Define a gauge of the application's own, at file scope.
 *
 * The name is what the server shows, so it's 1 to 32 ASCII letters, digits and underscores, written without quotes:
 * TRACEGALE_METRIC_GAUGE_DEFINE(battery_mv). Put the unit in the name. Each metric takes 16 bytes of RAM and its name in
 * flash. Values are whole numbers from -2,147,483,648 to 2,147,483,647, so scale what has a fraction: millivolts, not volts.
 */
#define TRACEGALE_METRIC_GAUGE_DEFINE(_name) Z_TRACEGALE_METRIC_DEFINE(_name, TRACEGALE_METRIC_GAUGE)

/** @brief Define a counter of the application's own, at file scope. See TRACEGALE_METRIC_GAUGE_DEFINE(). */
#define TRACEGALE_METRIC_COUNTER_DEFINE(_name) Z_TRACEGALE_METRIC_DEFINE(_name, TRACEGALE_METRIC_COUNTER)

/** @brief Use, in this file, a metric that another file defines. */
#define TRACEGALE_METRIC_DECLARE(_name) extern struct tracegale_metric tracegale_metric_##_name

/**
 * @brief Give a metric a new value. For a gauge, this is a reading.
 *
 * Can be called from any thread and from an interrupt. Tracegale sends the value at its next check-in, and keeps it until
 * the server has it.
 */
#define TRACEGALE_METRIC_SET(_name, _value) tracegale_metric_set(&tracegale_metric_##_name, (_value))

/**
 * @brief Add to a metric. For a counter, this counts something that happened. The amount can be negative.
 *
 * Can be called from any thread and from an interrupt. A total that would pass the largest value stays at it.
 */
#define TRACEGALE_METRIC_ADD(_name, _amount) tracegale_metric_add(&tracegale_metric_##_name, (_amount))

/** @brief What TRACEGALE_METRIC_SET() calls. */
void tracegale_metric_set(struct tracegale_metric *metric, int32_t value);

/** @brief What TRACEGALE_METRIC_ADD() calls. */
void tracegale_metric_add(struct tracegale_metric *metric, int32_t amount);

#else /* CONFIG_TRACEGALE_METRICS */

/* Without metrics, the same code builds, and costs nothing. */
#define TRACEGALE_METRIC_GAUGE_DEFINE(_name) extern struct tracegale_metric tracegale_metric_##_name
#define TRACEGALE_METRIC_COUNTER_DEFINE(_name) extern struct tracegale_metric tracegale_metric_##_name
#define TRACEGALE_METRIC_DECLARE(_name) extern struct tracegale_metric tracegale_metric_##_name
#define TRACEGALE_METRIC_SET(_name, _value) ((void)(_value))
#define TRACEGALE_METRIC_ADD(_name, _amount) ((void)(_amount))

#endif /* CONFIG_TRACEGALE_METRICS */

#ifdef __cplusplus
}
#endif

#endif /* TRACEGALE_TRACEGALE_H_ */
