# Firmware error codes

`tracegale status` and `tracegale_get_status()` show the last failure, and it stays set after a later success, for debugging. These
numbers are stable. `detail` adds the system's error number, an HTTP status or the server's error code, as shown.

| Code | Name | Meaning | Detail | What to check |
|---|---|---|---|---|
| 1 | `NOT_PROVISIONED` | The server or a credential is missing | None | Provision the device, including all three credentials |
| 2 | `DNS` | The server's name didn't resolve | Resolver error | The name, and that the application has DNS (`CONFIG_DNS_RESOLVER`) |
| 3 | `CONNECT` | No TCP connection | `errno` | That the application's network is up, the address and port, and any firewall |
| 4 | `TLS` | TLS failed | `errno`. `ECONNABORTED` is a failed handshake, and its number differs by platform: 103 on `native_sim`, 113 on Cortex-M | That the device has the server's CA, and that the certificate names the host you provisioned. The device's certificate must be from the server's CA, and credentials must be DER. On Zephyr v3.7, a refused connection can also show as this |
| 5 | `SEND` | The connection failed during a message | Negative `errno` | The link, and the server's logs for the message ID |
| 6 | `TIMEOUT` | No reply in time | Negative `errno` | The link, and `CONFIG_TRACEGALE_TIMEOUT_MS` |
| 7 | `HTTP_STATUS` | The reply wasn't status 200 | HTTP status | A proxy in between, or an overloaded server |
| 8 | `BAD_REPLY` | The reply broke the specification, or had the wrong ID | Internal reason | Decode it with `tools/wire/tracegale_wire.py`, and report a bug |
| 9 | `SERVER` | The server replied with an `ERROR` message | The server's code | See "Error codes" in `docs/protocol/README.md` |
| 10 | `TOO_LARGE` | A value or message doesn't fit | None | `CONFIG_TRACEGALE_CRED_MAX_SIZE` or `CONFIG_TRACEGALE_MAX_MESSAGE` |
| 11 | `STORAGE` | Credentials couldn't be stored or used | None | Settings storage, and `CONFIG_TLS_MAX_CREDENTIALS_NUMBER`. On the nRF91 series, the modem refuses new credentials, and refuses to erase them, while its link is up |
| 12 | `BAD_ARGUMENT` | A provisioning value is invalid | None | The value given |
| 13 | `REFUSED` | Over MQTT, the broker refused the connection or the subscription | The broker's CONNACK code, or 128 for a refused subscription | Code 5 is a certificate the broker doesn't accept: one that isn't from the device CA, or has been revoked. 128 means the broker's access rules don't allow the device's own topics, which is a server setup fault |
| 14 | `IDENTITY` | Over MQTT, the device's certificate names no identity | None | The certificate's common name must be the device's ID, as the server issues it. Only letters, digits, `-` and `_`, up to 36 characters |

## Crash uploads

A crash is sent on the same connection as a check-in, so its failures show with the same codes:

- `SERVER` with detail 7, `hash_mismatch`: the crash arrived damaged. It's sent again, and erased after three tries.
- `SERVER` with detail 5 or 13, `too_large` or `unsupported_kind`: this server won't take it, so it's erased.
- `SERVER` with detail 12, `server_busy`, or `TIMEOUT`: it stays stored, and the next attempt resumes where this one stopped.
- `BAD_REPLY`: the server's replies weren't progressing the upload. It stays stored.
- `STORAGE`: the stored crash couldn't be read.

`tracegale status` shows `crash waiting` until the server has all of it, then `crashes sent` counts it.

## Metrics

A report of metrics is sent on the same connection as a check-in, after any crash.

- `TIMEOUT` or another failure of the connection: the check-in fails, and the next report holds these values as well, with a
  longer period. `checkin_failures` in that report counts the failure.
- `SERVER` with detail 10 or 12, `rate_limited` or `server_busy`: the check-in still counts as a success, and the values are
  sent again at the next one.
- `SERVER` with any other detail, such as 3, `unsupported_type`, from a server older than metrics: the check-in still counts
  as a success, and the values are dropped, since the server would refuse them again.

`tracegale status` shows `metric reports sent`, and `tracegale metrics` shows what the next report would hold.

## Logs

Log records are sent after the metrics, on the same connection, with the same rules:

- `TIMEOUT` or another failure of the connection: the check-in fails, and the records stay for the next one. The server
  stores each record once, however often it's sent.
- `SERVER` with detail 10 or 12: the check-in counts as a success, and the records are sent again next time.
- `SERVER` with any other detail, such as 3 from a server older than logs: the check-in counts as a success, and the records
  that message held are dropped.

`tracegale status` shows `log records sent`, `waiting` and `dropped`, and `tracegale logs` lists what's waiting.

## Updates

An update is fetched on the same connection as a check-in, so a failure of the connection shows with the codes above, and the
download continues at the next check-in. How an update itself ended is separate: `tracegale status` shows it as
`last update result`, `tracegale_get_status()` gives it as `update_result`, and the server shows it for each device of a rollout.
These numbers are the `IMG_RESULT` outcomes in `docs/protocol/README.md`, and they're stable.

| Result | Name | Meaning | What to check |
|---|---|---|---|
| 0 | `NO_RESULT` | No update has ended on this device | Nothing |
| 1 | `INSTALLED` | The new image runs and is confirmed | Nothing |
| 2 | `NOT_STARTED` | MCUboot didn't start the image | That it's signed with the key the device's MCUboot was built with |
| 3 | `REVERTED` | The image started but wasn't confirmed, so MCUboot brought the previous one back | Whether the new image can reach the server within `CONFIG_TRACEGALE_OTA_CONFIRM_TIMEOUT_S`, and whether it crashed |
| 4 | `DAMAGED` | What was downloaded doesn't have the SHA-256 that was offered | The device's flash, if it repeats. It's downloaded again |
| 5 | `TOO_LARGE` | The image doesn't fit the second slot | The image's size against `slot1_partition`, and the hardware name of the release |
| 6 | `NOT_AN_IMAGE` | It doesn't start with an MCUboot header | That `zephyr.signed.bin` was uploaded |
| 7 | `DOWNGRADE` | It's older than the running image | The application's `VERSION` file, or `CONFIG_TRACEGALE_OTA_DOWNGRADE` |
| 8 | `STORAGE` | Flash or settings storage failed | The flash driver, and that settings storage has room |
| 9 | `UNSUPPORTED_KIND` | The offer wasn't for a full image | The server. This firmware installs full images only |

While an update is under way:

- `SERVER` with detail 9, `unknown_image`: the rollout was paused, ended or changed. The device asks what to do once more
  straight away, and after that at its next check-in.
- `SERVER` with detail 10 or 12, `rate_limited` or `server_busy`: the download continues at the next check-in.
- `BAD_REPLY`: the server sent a piece the device didn't ask for, or a first piece too short to hold an image header.
- `TOO_LARGE`: a piece didn't fit `CONFIG_TRACEGALE_MAX_MESSAGE`.

`tracegale status` shows `update:` with the state, which is `none`, `downloading`, `restart pending`, `testing` or `reporting`,
and the bytes downloaded so far.
