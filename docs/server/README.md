# Running the Tracegale server

Everything about running the server: settings, the API, single sign-on, backups, upgrades and monitoring. To install it
step by step, see Install the server on the docs site.

You need Docker with Compose, and nothing else: setup creates its own certificates and secrets.

## Start

```sh
cd server
TRACEGALE_SERVER_NAMES=fleet.example.com docker compose up -d
docker compose exec server /tracegale setup-code
```

Then open the admin side and create the first administrator's account. The setup page asks for that code, which proves you run
the server. It works once, for an hour.

`TRACEGALE_SERVER_NAMES` lists the names or IP addresses devices connect to, separated by commas. The first is what new devices get told to use. Without it, the
server uses `localhost`, which real devices can't verify, and setup warns about it. You can change it later, and existing devices keep working, because
they trust the CA and not this certificate.

| Port | Used by | Default |
|---|---|---|
| 8443 | Devices, over HTTPS | Open to the network |
| 8883 | Devices, over MQTT | Open to the network |
| 8444 | Administrators | This machine only |

To reach the admin side from elsewhere, set `TRACEGALE_ADMIN_BIND`, and put it behind a firewall, a VPN or a reverse proxy with its own
sign-on (see below). If no device uses MQTT, close port 8883 in your firewall, or set `TRACEGALE_MQTT_PORT` to bind it to this
machine only, such as `127.0.0.1:8883`.

Browsers warn about the admin page, because the certificate comes from the server's own CA.

## Add a device

Open `https://localhost:8444` and log in. The overview shows the fleet's health. Under **Devices**, choose **Add a device**. The page shows the shell commands that provision it. **The private key appears only
once.** The server doesn't keep it.

Or use the API:

```sh
curl --insecure -H "Authorization: Bearer $TOKEN" -d '{"name":"pump 1"}' https://localhost:8444/api/v1/devices
```

The firmware side is in `docs/firmware/README.md`.

`$TOKEN` is an API token, from your account page. See "Accounts and API tokens".

## Accounts and API tokens

| Task | How |
|---|---|
| Add someone | **Members**, then **Add an account**, with a role in this organization. Give them the temporary password it shows once; they choose their own when they first log in |
| Change what someone can do | **Members**, then choose their role in this organization. Viewers read the fleet; operators also act on it; administrators also manage members, projects and signing keys and read the audit log |
| Organize devices | Each project holds its own devices, crashes, symbol files, releases, rollouts, metrics and logs. An organization's administrators add projects on **Members**; server administrators add organizations on **Organizations** |
| Choose the project in the API | Add `?project=<short name>` to every call, unless the token's account can see only one project. A call that needs a project and doesn't name one gets 400, with the account's projects listed |
| See which firmware the fleet runs | **Overview**, under **Firmware in the fleet**, or `GET /api/v1/fleet/versions`: devices by firmware version and hardware, with how many are online |
| See who did what | **Accounts**, then **Audit log**, or `GET /api/v1/audit` with an administrator's token, filtered by `user` and `action`, paged with `before` |
| Change your password | Click your username at the top, then **Change password**. Your other browsers are logged out |
| Create an API token, for a script or build machine | Your account page, under **API tokens**. It's shown once, so copy it into the machine's secret store. It acts as you, with your role at the time it's used. A build machine needs an operator's token |
| Remove someone | **Accounts**, then **Disable**. They're logged out, and their API tokens stop working |
| Forgotten password | Another administrator disables and re-adds them, or on the server: `docker compose exec server /tracegale reset-password <username>` |
| An API token without the web view | `docker compose exec server /tracegale create-api-token <username> <name>` |

Passwords need at least 12 characters, and are stored with Argon2id. After 10 wrong passwords an account waits 15 minutes, and
each address can try only 10 logins a minute. Sessions last 12 hours.

Accounts made before roles existed became administrators, and those made before organizations became server administrators and
administrators of the organization "Default", whose project "default" holds everything that existed, so nothing changed for
anyone. Server administrators disable accounts and add organizations; the last active one can't be disabled or stop being one. A request a role doesn't allow gets 403, and the server logs it as `refused by role`.

## Single sign-on

Tracegale can let people sign in through your organisation's OpenID Connect provider, such as Keycloak, Authentik, Entra ID,
Okta or Google. Their groups at the provider decide their role, at every sign-on, so access is managed in one place.

1. At the provider, register Tracegale as a confidential web application with the redirect URI
   `https://<your admin address>/login/oidc/callback`, and have it put the person's groups in the ID token, in a claim named
   `groups` unless you say otherwise.
2. Put the client secret in a file the server can read, for example `/opt/tracegale/server/oidc-client-secret`, readable by
   user 65532, and mount it with a `compose.override.yaml`:

   ```yaml
   services:
     server:
       volumes:
         - ./oidc-client-secret:/run/oidc-client-secret:ro
   ```
3. Set these, in `.env` or the environment, and run `docker compose up -d`:

| Variable | Meaning |
|---|---|
| `TRACEGALE_OIDC_ISSUER` | The provider's issuer, an `https://` address. Single sign-on is off without it |
| `TRACEGALE_OIDC_CLIENT_ID` | The client ID the provider gave Tracegale |
| `TRACEGALE_OIDC_CLIENT_SECRET_FILE` | The secret's path inside the container, such as `/run/oidc-client-secret`. A file, so the secret isn't in the container's environment |
| `TRACEGALE_OIDC_REDIRECT_URL` | The redirect URI from step 1, exactly |
| `TRACEGALE_OIDC_LABEL` | The provider's name on the login button, such as `Company SSO` |
| `TRACEGALE_OIDC_GROUPS_CLAIM` | The claim that lists groups, `groups` by default |
| `TRACEGALE_OIDC_ADMIN_GROUPS`, `TRACEGALE_OIDC_OPERATOR_GROUPS`, `TRACEGALE_OIDC_VIEWER_GROUPS` | Comma-separated groups that give each role. Someone in several gets the most; someone in none can't sign in |

- The login page gets a **Log in with** button. Password logins keep working, for accounts that have a password.
- **The first sign-on makes an account**, named after the person's username or email address at the provider, with no
  password. It's linked to the provider's issuer and its unchanging subject for the person, never to an email address, which
  people can sometimes change themselves.
- **The role follows the groups at every sign-on.** An administrator can still change it, until the person's next sign-on.
  The last active administrator keeps the role, so someone can always manage accounts.
- **To remove someone**, take them out of the groups at the provider. To stop them at once, also disable their account here:
  a session already open lasts up to 12 hours.
- Sign-on uses the authorization code flow with PKCE, a state and a nonce, and checks the ID token's signature, issuer,
  audience and expiry. The server can start while the provider is down; the button then says it isn't answering.

## MQTT

Devices built with `CONFIG_TRACEGALE_TRANSPORT_MQTT` connect to port 8883 instead of 8443, with the same credentials, and get
the same answers: the server handles each message with the same code, whichever way it came. Nothing about a device needs
setting up for MQTT, and one fleet can use both.

The `broker` service is Eclipse Mosquitto, in a container of its own, as nginx is for HTTPS:

- It uses the same server certificate as nginx, and accepts only devices with a certificate from the device CA, which it
  checks during TLS. A device's identity is its certificate's common name. It's the device's MQTT username and client ID,
  whatever the device asks for, so one device can't take over another's connection.
- Its access rules let each device publish only to `tg/v1/<identity>/up` and read only `tg/v1/<identity>/down`. The server
  trusts the identity in a topic because of these rules.
- It keeps nothing: no retained messages and no stored sessions, since every message is a request or its reply.
- The server reaches it through a Unix socket in a volume only the two of them mount, so it needs no password and no port.
  The server subscribes to every device's `up` topic and publishes each reply to the device's `down` topic. If the broker is
  down, the server keeps reconnecting, and HTTPS isn't affected.

A revoked device is refused at its next message, as over HTTPS. The broker itself doesn't read the revocation list, so a
revoked device can still connect to it, but the server answers nothing it sends except with an error.

**Behind a proxy**, pass port 8883 through as raw TCP, as for the HTTPS device port, and for the same reason.

## Revoke a device

Open the device's page and choose **Revoke device**, which asks you to confirm, or use `POST /api/v1/devices/<id>/revoke`. It takes effect at the device's next message, and can't be undone in this version.

**If a device's key may have leaked**, for example a device was lost or opened up, revoke it, then add it again as a new
device under **Add a device** and provision it with the new credentials. The old key is refused from the moment of the
revocation. Replacing a device's credentials remotely, without provisioning it again, is planned for after v1.

## Crashes

A device sends a crash after it restarts. The server decodes it with the firmware's ELF file, and groups it with others that have
the same reason and the same first three functions into an issue. Line numbers aren't part of that, so a rebuild that moves code
still groups with earlier crashes.

**Upload the ELF file of every build you ship**, on the **Symbol files** page: choose the file and **Upload**. The server finds
it by the build ID inside it. Or upload every build from the build machine, with an API token:

```sh
docker compose exec server /tracegale server-ca > server-ca.pem
curl --cacert server-ca.pem -H "Authorization: Bearer $TOKEN" \
  --data-binary @build/zephyr/zephyr.elf "https://localhost:8444/api/v1/symbols?name=zephyr.elf"
```

Uploading the same file again does nothing, and a different file with the same build ID is refused. Crashes that arrived before
their file are decoded as soon as it's uploaded. Keep the ELF files from releases anyway: they can't be rebuilt byte for byte.

In the web view, **Issues** lists the groups, an issue shows its latest backtrace and its crashes, and each crash shows its
backtrace, registers and a download of the raw coredump, for GDB with Zephyr's `scripts/coredump/coredump_gdbserver.py`.

| API | What it does |
|---|---|
| `POST /api/v1/symbols?name=<name>` | Upload an ELF file as the body |
| `GET /api/v1/symbols` | List symbol files |
| `GET /api/v1/issues`, `GET /api/v1/issues/<id>` | Issues, and one issue with its crashes |
| `GET /api/v1/crashes`, `?undecoded=true` | Recent crashes, or only those not decoded |
| `GET /api/v1/crashes/<id>`, `.../coredump` | One crash, or its raw coredump |

A crash's status says what's happening:

| Status | Meaning |
|---|---|
| decoding | Waiting for the decoder, or being retried after a failure, which the crash's problem field explains |
| waiting for symbols | No ELF file with its build ID has been uploaded |
| decoded | It has a backtrace, and belongs to an issue |
| not decoded | It can't be: it isn't a Zephyr coredump, it has no build ID, or the decoder failed three times |

**What can be decoded:** Arm Cortex-M and RISC-V. On Zephyr v3.7, only the crashing function and its caller are shown on Cortex-M,
and only the crashing function on RISC-V, because v3.7 doesn't record the stack pointer. The page says so on those crashes.

**The decoder** is a separate container, because coredumps come from devices and can't be trusted (threat T9). It has no network
at all, a read-only root, no capabilities, its own user, and limits on memory, processes, CPU and time. It can read the symbol
files and nothing else. If it's down, crashes wait, and are decoded once it's back.

**Coredumps hold device memory,** which can include secrets that were on a stack, and ELF files can hold your source paths and
constants. Only administrators can see either.

## Updates

Tracegale sends new firmware to devices over the connection they already have. The server stores signed images and decides
which device is offered which. It **never signs anything and never holds a signing key**: MCUboot in each device checks the
signature with a key built into it, so a breached server can't make a device run firmware you didn't sign (threat T1). The
device's side is in `docs/firmware/README.md`, and the step-by-step guide is **Help**, **Update your devices**.

**Signing keys.** Register the public half of your signing key under **Updates**, **Signing keys**, or with the API. From then
on, an upload must be signed by a registered key, and a release that isn't can't be rolled out. With no key registered, the
server accepts any signed image without checking who signed it, and the pages say so. Images signed with MCUboot's development
keys are always refused, and so are those keys themselves, because their private halves are public.

**A release** is one signed image for one hardware name. Upload `zephyr.signed.bin` on the **Updates** page, with the hardware
name and an optional note, or from the build machine:

```sh
curl --cacert server-ca.pem -H "Authorization: Bearer $TOKEN" \
  --data-binary @build/zephyr/zephyr.signed.bin \
  "https://localhost:8444/api/v1/releases?hardware=nrf52840dk/nrf52840&note=Fixes+the+cold+start"
```

The hardware name must be exactly what the devices report, which each device's page shows as **Hardware**. The server reads
the version from the image, checks its hash and signature, and refuses a file that isn't a signed MCUboot image, or is
encrypted, padded or already confirmed. Uploading the same image for the same hardware again changes nothing.

**A rollout** offers one release to the devices that report its hardware: all of them or one cohort, and a percentage of
those. A cohort is a name you give devices on their pages, such as `beta`. Which devices fall within a percentage is fixed
for each rollout and device, so raising it only adds devices. When two running rollouts match a device, it's offered the
one started last.

A device is told at its check-in that an update may be waiting, asks, downloads the image in pieces, restarts into it, and
reports how it went. It downloads nothing when it already runs the image or something newer.

| A device's status | Meaning |
|---|---|
| offered | It has been offered the image, and hasn't reported the end yet. This includes a device that's trying again after a failure |
| installed | It runs the image, and reached the server with it |
| failed | It failed three times, or in a way that trying again can't fix. It isn't offered this rollout again |

**Pausing** a rollout stops offers and downloads until it's resumed. **Ending** one is final. Neither brings old firmware
back on devices that installed the image, and devices refuse versions older than the one they run. To replace a bad
release, upload one with a higher version and roll that out. A device that had already downloaded the whole image may
still install it at its next restart.

**A release can be deleted** once all its rollouts have ended, which also deletes their history. **Deleting a signing key**
marks its releases as not checked. Their running rollouts carry on, so end them yourself if the key was stolen.

| API | What it does |
|---|---|
| `GET /api/v1/signing-keys`, `POST` with `{"name", "public_key"}` | List the keys, or register one from its PEM public half |
| `DELETE /api/v1/signing-keys/<id>` | Delete a key |
| `POST /api/v1/releases?hardware=<name>&note=<text>` | Upload a signed image as the body. 201, or 200 with `already_uploaded` |
| `GET /api/v1/releases`, `GET /api/v1/releases/<id>` | List the releases, or one with its rollouts |
| `DELETE /api/v1/releases/<id>` | Delete a release. 409 while a rollout of it is running or paused |
| `POST /api/v1/rollouts` with `{"release_id", "cohort", "percent"}` | Start a rollout. An empty cohort means every device |
| `GET /api/v1/rollouts`, `GET /api/v1/rollouts/<id>` | List the rollouts, or one with its devices and their results |
| `POST /api/v1/rollouts/<id>/percent` with `{"percent"}` | Change the percentage |
| `POST /api/v1/rollouts/<id>/pause`, `.../resume`, `.../abort` | Pause, resume or end a rollout. 409 when its state doesn't allow it |
| `POST /api/v1/devices/<id>/cohort` with `{"cohort"}` | Put a device in a cohort. An empty name takes it out |

Images are kept in the database, so a database backup includes them. The server keeps recently used images in memory, up to
`TRACEGALE_IMAGE_CACHE_MB`, so that a fleet downloading one image doesn't read it from the database for every piece. Downloads
have their own rate limit for each device, apart from the limit on check-ins.

## Metrics

Devices report metrics with each check-in (`docs/firmware/README.md`, "Metrics"). **Metrics** in the web view shows them for
the whole fleet, and each device's page links to its own. Help, "Watch your fleet's metrics", explains the charts.

| API | What it does |
|---|---|
| `GET /api/v1/metrics` | Every metric name the fleet has reported, with its kind, `gauge` or `counter` |
| `GET /api/v1/metrics/<kind>/<name>?range=` | The fleet's figures for each hour and firmware version: count, sum, lowest and highest. `range` is `24h` (the default), `7d`, `30d`, `90d` or `1y` |
| `GET /api/v1/devices/<id>/metrics?range=` | The names a device reported in the period. `range` is `24h`, `7d` or `30d` |
| `GET /api/v1/devices/<id>/metrics/<kind>/<name>?range=` | A device's reports of one metric, each with its time, value, period and firmware version |

The firmware's own metrics have names starting with `sys.`, such as `sys.heap_free`, so they never mix with an application's.

**Storage.** Reports are kept in a table split by day, and whole days are dropped after `TRACEGALE_METRICS_RETENTION_DAYS`.
The fleet's figures, one row for each metric, firmware version and hour, are kept for
`TRACEGALE_METRICS_FLEET_RETENTION_DAYS`. A report with nine values took about 760 bytes, its index included, when measured
on 2026-11-15. A fleet of 100,000 devices that checks in hourly then needs about 1.8 GB a day, or 55 GB for 30 days. Size the
database for your fleet, or shorten the retention.

**Limits.** Reports have their own rate limit for each device, apart from check-ins. The fleet may have
`TRACEGALE_METRICS_MAX_NAMES` different names; values of further names are dropped, and the server's log says
`metric values dropped`. Raise the limit if your firmware really has that many.

## Logs

Devices with `CONFIG_LOG=y` send what they logged with each check-in. **Logs** in the web view searches them, and each
device's page shows its latest. Help, "Read your devices' logs", explains the page.

| API | What it does |
|---|---|
| `GET /api/v1/logs` | Records, newest first. `q` is text the message contains, ignoring case; `level` is `error`, `warning`, `info` (the default) or `debug`, and shows that level and the more severe ones; `device` is a device ID; `range` is `1h`, `24h` (the default), `7d` or `14d`; `limit` is 1 to 500, 100 by default. A reply with `next` has more: pass it back as `before` |

Each record has the device, when it was logged (`at`, worked out from the device's uptime; `at_known` is false when the
device couldn't say, and `at` is then when it arrived), when it arrived, the device's start (`boot`) and the record's number
in it (`seq`), the level from 1, error, to 4, debug, the module, the message, the firmware version and the ID of the message
that brought it.

**Storage.** Records are kept in a table split by the day they arrived, and whole days are dropped after
`TRACEGALE_LOGS_RETENTION_DAYS`. A trigram index on the text keeps a search of the whole fleet fast. Each device may store
`TRACEGALE_LOGS_MAX_PER_DEVICE_DAY` records a day; the rest are acknowledged and dropped, and the server's log says
`log records over the device's daily limit were dropped`. The count starts again each day, in UTC, and when the server
restarts.

## Settings

| Variable | Default | Meaning |
|---|---|---|
| `TRACEGALE_SERVER_NAMES` | `localhost` | Names devices connect to |
| `TRACEGALE_DEVICE_PORT` | `8443` | Port for devices, over HTTPS |
| `TRACEGALE_MQTT_PORT` | `8883` | Port for devices, over MQTT. Docker's port syntax, so `127.0.0.1:8883` keeps it on this machine |
| `TRACEGALE_ADMIN_PORT` | `8444` | Port for administrators |
| `TRACEGALE_ADMIN_BIND` | `127.0.0.1` | Address the admin port listens on |
| `TRACEGALE_EVENT_RETENTION_DAYS` | `90` | How long check-in records are kept. Whole days go at once |
| `TRACEGALE_CRASH_RETENTION_DAYS` | `90` | How long crashes and their coredumps are kept. Issues keep their counts |
| `TRACEGALE_METRICS_RETENTION_DAYS` | `30` | How long each device's reports of metrics are kept, from 1 to 3660. Whole days go at once |
| `TRACEGALE_METRICS_FLEET_RETENTION_DAYS` | `400` | How long the fleet's hourly figures are kept, from 1 to 3660 |
| `TRACEGALE_METRICS_MAX_NAMES` | `1000` | How many different metric names the fleet may have, from 20 to 100,000. Values of names beyond it are dropped |
| `TRACEGALE_LOGS_RETENTION_DAYS` | `14` | How long devices' log records are kept, from 1 to 3660. Whole days go at once |
| `TRACEGALE_LOGS_MAX_PER_DEVICE_DAY` | `10000` | Log records each device may store a day, from 100 to 10,000,000 |
| `TRACEGALE_AUDIT_RETENTION_DAYS` | `400` | How long the audit log keeps events, from 30 to 3660 |
| `TRACEGALE_SYMBOL_MAX_MB` | `256` | Largest ELF file accepted |
| `TRACEGALE_IMAGE_MAX_MB` | `16` | Largest firmware image accepted, from 1 to 1024 |
| `TRACEGALE_IMAGE_QUOTA_MB` | `2048` | Most space all of a project's firmware images may take together, in the database. An upload past it is refused; delete old releases |
| `TRACEGALE_IMAGE_CACHE_MB` | `256` | Memory for firmware images that devices are downloading. At least `TRACEGALE_IMAGE_MAX_MB` |
| `TRACEGALE_BACKUP_RECIPIENT` | None, so `backup.sh` refuses | The age public key (`age1...`) backups are encrypted to. See "Back up" |
| `TRACEGALE_TRUSTED_PROXY` | `127.0.0.1/32`, which trusts nothing | The one reverse proxy, as an address or range, whose `X-Forwarded-For` gives an administrator's real address. See below |

## Behind a reverse proxy, such as Pangolin

To reach Tracegale from outside your network, put a reverse proxy in front of it. Devices and administrators need different
treatment:

| Port | Proxy it as | Why |
|---|---|---|
| Devices, `TRACEGALE_DEVICE_PORT` and `TRACEGALE_MQTT_PORT` | **Raw TCP, passed straight through** | Devices prove who they are with their own certificate during TLS. A proxy that ended TLS would hide it, and every device would be refused |
| Admin, `TRACEGALE_ADMIN_PORT` | Normal HTTPS. The proxy can end TLS and connect to Tracegale over HTTPS | Tracegale's certificate comes from its own CA, so the proxy needs to skip verifying it, or trust that CA |

- List the public name first in `TRACEGALE_SERVER_NAMES`, because devices check it against Tracegale's own certificate.
- A device port can share a public port with other services if the proxy picks the route by the name the client sends at the
  start of TLS (SNI), still passing the connection through untouched. Traefik calls this a TCP router with `HostSNI`. Tracegale's
  firmware sends the name it was provisioned with, so provision devices with the public name, not an address.
- Set `TRACEGALE_ADMIN_BIND` to the LAN address the proxy reaches, and `TRACEGALE_TRUSTED_PROXY` to the proxy's address. Without
  it, every administrator seems to come from the proxy, and they share one login rate limit.
- If the proxy adds its own sign-on, let `/api/v1/symbols` through, so build machines can upload symbol files with their token.
  The web view's upload forms, at `/symbols` and `/releases`, need the same large-upload settings as the API's.
- Uploads through a proxy are limited by its timeouts. Pangolin's is 30 minutes per request.

The Pangolin setup for this project is in the `pangolin-infra` repository, handoffs 021 to 024.

## Upgrade

```sh
./upgrade.sh <version> /var/backups/tracegale
```

It takes a backup into that folder first (see "Back up"; it needs `TRACEGALE_BACKUP_RECIPIENT`), moves the checkout to the
version, a tag, commit or branch such as `main`, rebuilds, waits until every service is healthy, and restarts nginx, which reads
`deploy/nginx.conf` only when it starts. If the new version doesn't start, it says how to go back. `--no-backup` in place of the
folder skips the backup, for a server whose data you can lose.

The footer of every page in the web view says which version is running, such as "Tracegale 1.0.0 (build c1a79f5)". The
build is the commit `upgrade.sh` built from, which tells apart two servers between releases. `/tracegale version` prints
the same, and the monitoring metric `tracegale_build_info` has both. The sign-in page doesn't show it, so only people with
an account can see which version a server runs.

By hand, the same without the backup:

```sh
git pull
TRACEGALE_COMMIT=$(git rev-parse HEAD) docker compose up -d --build --wait
docker compose restart nginx
```

**What can upgrade to what:** any version to any later one, in one step. The database migrates itself at startup, through
every migration in between, under a lock. Each migration is tested on a database from the version before it: the files in
`server/internal/store/testdata/upgrade/` are real databases from earlier versions, which every server must migrate with
their data intact.

**Going back** to an older version isn't an upgrade: the server refuses a database newer than itself, so a migrated database
can't be used by the version before. Go back by restoring the backup taken before the upgrade, with the old version, onto a new
install.

**Never run `docker compose down -v`** on a real install, except to start a restore over. It deletes the database and the CA
keys, and without a backup every device would have to be provisioned again.

## Back up

`./backup.sh` writes an encrypted backup of the whole server to standard output, while it runs: the database, the CA keys and
other secrets, and the symbol files. Restoring it on a new host brings everything back, and devices reconnect with the
credentials they already have.

**Once, make a key pair**, on a machine that isn't the server, with the standard [age](https://age-encryption.org) tool:

```sh
age-keygen -o tracegale-backup.key
```

or, without age, on the server, moving the file off it at once:

```sh
docker compose run --rm backup backup-key > tracegale-backup.key
```

Put the public key it prints (`age1...`) in `.env` as `TRACEGALE_BACKUP_RECIPIENT`. Keep `tracegale-backup.key` somewhere
safe and away from the server, such as a password manager and a printed copy: **without it no backup can be restored**, and
anyone who has it and a backup has your device CA's key. The server holds only the public key, so someone who takes over the
server can't read its old backups.

**Each backup:**

```sh
./backup.sh > tracegale-$(date -u +%Y%m%dT%H%M%SZ).tgbackup
```

Copy each file off the host. Daily is a sensible start; a crontab line on the server:

```
15 3 * * * cd /opt/tracegale/server && ./backup.sh > /var/backups/tracegale/tracegale-$(date -u +\%Y\%m\%dT\%H\%M\%SZ).tgbackup
```

A backup is a tar file. `MANIFEST.json` says when it was made and the database's version; nothing secret. The two `.age` parts
are the database (`pg_dump` in custom format) and the volumes, encrypted to your key as they're produced, so the plain
database and keys never reach a disk. The standard age tool opens them too: `age -d -i tracegale-backup.key
database.dump.age > database.dump`.

What a backup doesn't hold: `.env` and your checkout's version. Keep a copy of `.env`, and note the version you run.

## Restore

Onto a new install that has never started: a fresh checkout of the same version or a later one, with your `.env`.

```sh
./restore.sh tracegale-20261115T031500Z.tgbackup tracegale-backup.key
```

It restores the keys and symbol files, starts the database and restores it, then starts everything. A backup from an older
version is upgraded as the server starts; one from a newer version is refused. It refuses an install that has already run, or
whose volumes hold keys, so it can never replace a working CA. If a restore stops part way, remove that new install's
containers and volumes with `docker compose down -v`, **on the new host only**, and run it again.

If the new host has another name, set `TRACEGALE_SERVER_NAMES` before restoring: the server gets a new certificate from the
restored server CA, which devices trust. Devices still connect to the name they were given, so point that name at the new
host.

`server/test/restore_drill.py` runs this whole procedure, with a real device, in the project's full checks.

## Monitor

The server's own measurements are at `/api/v1/server/metrics` on the admin port, in Prometheus' text format, for a server
administrator's API token. A Prometheus job:

```yaml
scrape_configs:
  - job_name: tracegale
    scheme: https
    metrics_path: /api/v1/server/metrics
    authorization:
      credentials_file: /etc/prometheus/tracegale-token   # a server administrator's API token
    tls_config:
      ca_file: /etc/prometheus/tracegale-ca.pem           # from: docker compose exec server /tracegale server-ca
    static_configs:
      - targets: ["tracegale.example.com:8444"]
```

| Measurement | What it says |
|---|---|
| `tracegale_device_messages_total{type, result}` | Every device message answered, over HTTPS and MQTT, by type (`hello`, `metrics`, `logs`, `img_get`, ...) and result (`ok`, `server_busy`, `rate_limited`, `not_authorized`, ...) |
| `tracegale_device_message_seconds_total{type, result}` | Time spent answering them; divided by the count, the average |
| `tracegale_device_requests_in_flight`, `..._limit` | Messages being answered now, and the most at once |
| `tracegale_devices{state}` | Active, online and revoked devices, in every organization |
| `tracegale_db_connections{state}`, `tracegale_db_connection_waits_total` | The database pool, and how often the server waited for a connection |
| `tracegale_database_bytes`, `tracegale_table_bytes{table}`, `tracegale_symbol_files_bytes` | Storage, by table |
| `tracegale_disk_free_bytes` | Free space on the disk with the symbol files, usually the database's too |
| `tracegale_retention_last_success_timestamp_seconds{step}`, `tracegale_retention_failures_total{step}` | Each hourly clean-up |
| `tracegale_mqtt_bridge_connected` | 1 while the server is connected to the MQTT broker |

Alerts worth having: `server_busy` or `rate_limited` results growing; in-flight messages near the limit; connection waits
growing; free disk under 10%; a retention step that hasn't succeeded for a day; the MQTT bridge at 0 for more than a few
minutes. Back it with a check from outside that the device port answers.

**Check these settings:** the overview page shows server administrators settings to fix: devices told to connect to a name
only this machine has, backups not set up, a proxy setting that trusts far more than one proxy, and a nearly full disk. The
server's log says them too, when it starts.

## Debug

```sh
docker compose ps
docker compose logs server
```

Logs are JSON. Each device message logs its device and message ID, the same ID `tracegale status` shows on the device, and `device_events` stores it.

## What each part can see

| Service | Can reach | Secrets it can read |
|---|---|---|
| `nginx` | The network and the Unix sockets | The server certificate's key |
| `broker` | The network and its own socket. Not the database's network | The server certificate's key |
| `server` | The database, its own sockets, the decoder's socket and the broker's | Device CA key, session key, database password |
| `decoder` | Nothing: it has no network. Only the server reaches its socket. GDB runs as a user of its own for each worker, which can write nowhere | None. It reads symbol files, and can't write them |
| `db` | Nothing outside. It runs as PostgreSQL's own user, never root | Database password |
| `init` | Nothing at all. It runs once at startup | All of them, because it creates them. It's the only one with the server CA's key |
| `backup` | The database. It runs only when `backup.sh` or `restore.sh` starts it | All of them, to back them up and restore them. It encrypts them before they leave it, and holds no key that decrypts |

`server` and `db` have no route to the internet. Containers run read-only with no capabilities, except `db`, which needs to write, `init`, which needs to set file owners, and `decoder`, which may change users and nothing else, to run GDB as a user of its own.
