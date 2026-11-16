# First integration on real hardware

**Headline:** Tracegale 1.0.0-rc.1 was added to a production nRF52840 board whose only link is a SIMCom SIM7600G cellular
modem running MQTT over TLS itself. The board checked in through the modem to a live Tracegale server, reached from the
Internet through a reverse proxy, and its metrics and logs arrived. Getting there took more effort than it should have. The
14 points of friction found along the way are each fixed, documented, or recorded below, and provisioning was rebuilt so a
device needs no commands at all: enrollment, in 1.0.0-rc.2.

## What was tested

Whether a team that isn't Tracegale's can add it to a real product, on a link Tracegale has no binding for, using only the
public firmware repository and its documentation.

The integration was done by the user's own AI agent, working in the product's repository and told to use only
`tracegale-firmware` as its source. It was also told to write down everything that got in its way. The user ran the board, and
reported two things directly: the web app gave the wrong port for MQTT, and provisioning needed too much copying and pasting.

## The setup

- **The board:** a production nRF52840 board, two hardware revisions on one board target, built with the nRF Connect SDK
  v3.4.2 (Zephyr v4.4), sysbuild and MCUboot.
- **The link:** an onboard SIMCom SIM7600G, driven by AT commands. The modem runs TLS and MQTT itself, so the board has no
  Zephyr networking. The modem may only be used in a short window, when the application releases it, and its power is then
  removed.
- **Tracegale:** firmware `v1.0.0-rc.1`, as `CONFIG_TRACEGALE_TRANSPORT_CUSTOM`, with the product's own MQTT driver for the
  modem; OTA left to the product's own update path (`CONFIG_TRACEGALE_OTA=n`).
- **The server:** the live Tracegale server, `1.0.0-rc.1`, reached through the user's reverse proxy, with devices on raw TCP
  passthrough.

## Results

| What | Result |
|---|---|
| `CONFIG_TRACEGALE=y` builds on the nRF Connect SDK v3.4.2 | Yes, after turning off `CONFIG_TRACEGALE_CRASH_REBOOT` (point 5) |
| Cost with a custom transport, where the modem does TLS | About 18 KB of flash and 11 KB of RAM |
| A custom transport written from the protocol document alone | Yes, without reading the server's code |
| TLS with the device's own certificate, verified by the broker | Yes, once the port was right |
| A check-in through the modem to the live server, with metrics and logs | Yes: `successes: 1`, metrics and logs on the server |
| Provisioning kept across reboots and a firmware update | Yes, in settings storage |
| Provisioning with the web app's commands | Failed: wrong port for MQTT (point 12); credentials pasted twice were accepted (point 14) |

## Friction, and what was done

| # | What got in the way | What's done | Where |
|---|---|---|---|
| 1 | A custom transport needs Tracegale's internal headers, and the identity code was built only for MQTT | A public header, `include/tracegale/transport.h`, with the whole contract; the identity code builds for custom transports | rc.2 |
| 2 | The transport's lifecycle wasn't written down: no `close()` after a failed `open()` | Documented in the header and the firmware guide | rc.2 |
| 3 | No way to know when an on-demand check-in finished | `tracegale_check_in()` makes one attempt and returns its result | rc.2, `tests/link` |
| 4 | A link that's usually off: scheduled attempts failed, counted as failures, and showed a healthy device failing | `tracegale_link_set()` and `CONFIG_TRACEGALE_LINK_UP_AT_BOOT=n`: no attempts while it's down, none counted, status `waiting for link` | rc.2, `tests/link` |
| 5 | Two fatal error handlers on the nRF Connect SDK failed to link | Tracegale's is off with the SDK's `CONFIG_RESET_ON_FATAL_ERROR` | rc.2 |
| 6 | Nothing is kept without a transport | Recorded as a known limit, for after v1 | Later |
| 7 | Credential backends' expectations unclear | A custom transport reads credentials in `open()`, documented; the backends are internal and not used with one | rc.2 |
| 8 | Binary messages weren't stated, and the modem's MQTT refused non-UTF-8 payloads by default | Stated in the protocol and the firmware guide, with the SIM7600's setting as an example | rc.2 |
| 9 | What "verify the server" means without a clock | Stated: chain, signature and name always; expiry when the device knows the time | rc.2 |
| 10 | Setup assumed the application owns the west manifest | Setup covers a git submodule with `ZEPHYR_EXTRA_MODULES` | rc.2 |
| 11 | Two hardware revisions share one board target, so one could be offered the other's update | The hardware-name note covers revisions | rc.2 |
| 12 | The web app gave the HTTPS port, 8443, to an MQTT device, which needs 8883; the device said only `last error 3` | **Add a device** asks HTTPS or MQTT and gives that port; `tracegale server <host>` takes the binding's port; a mismatched port warns | rc.2 |
| 13 | Pages didn't update without a reload | Overview, devices, device, issues and crash pages reload every 30 seconds, with a link to stop | rc.2 |
| 14 | A piece pasted twice was stored, and TLS failed with no clue | `tracegale commit` checks each credential's DER structure and that the key belongs to the certificate; commands start by clearing; the page shows expected totals | rc.2, `tests/core` |
| User | "Provisioning the MCU is very hard, there are a lot of copy-paste procedure" | Enrollment: a build file and a token, and each device gets its own certificate on first boot, with nothing pasted; tested end to end over HTTPS and MQTT | rc.2 |

## Limits

- One board, one modem, one network, on one day. The check-in worked once of two attempts in the first run; the other lost its
  connection (`last error 5`), with the cause not found.
- Crash reports weren't triggered on the board, and OTA was off by choice, so neither was tested here.
- The fixes are tested in Tracegale's simulator tests, not yet on this board: that needs the next release candidate.

## How to repeat

Follow `docs/firmware/README.md` for a custom transport, with the product's own link driver, and provision with the web app's
**Add a device** commands. The product's repository and its notes are private to the user.
