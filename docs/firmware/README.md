# Using Tracegale in firmware

Everything about the firmware module: each feature, its options, how it behaves when things go wrong, and what it costs in
flash and RAM. New to Tracegale? Start with the step-by-step guide to adding it to your firmware, in the in-app Help or on the docs
site's Get started.

Tracegale uses the connection your application already has, and never turns on networking by itself.

## Setup

1. Add Tracegale's repository to your workspace's `west.yml`, then run `west update`:

    ```yaml
    manifest:
      remotes:
        - name: tracegale
          url-base: https://github.com/jvrpapa05
      projects:
        - name: tracegale-firmware
          remote: tracegale
          revision: main
          path: modules/lib/tracegale
    ```

    Zephyr finds the module by itself. For products, pin `revision` to a release tag.

    **If the manifest isn't yours,** as when you build against an nRF Connect SDK installed by `nrfutil sdk-manager` or VS Code,
    don't edit the SDK's `west.yml`: a reinstall loses the change. Instead, keep Tracegale in your application's own
    repository, as a git submodule, and name it when you build:

    ```
    git submodule add https://github.com/jvrpapa05/tracegale-firmware modules/tracegale
    git -C modules/tracegale checkout v1.0.0-rc.1
    west build -b <board> -- -DZEPHYR_EXTRA_MODULES=$PWD/modules/tracegale
    ```

    Or set it once, in your application's `CMakeLists.txt`, before `find_package(Zephyr)`:
    `list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/modules/tracegale)`. With sysbuild, that line reaches only
    the application, which is all that needs Tracegale.

2. Add `CONFIG_TRACEGALE=y` to `prj.conf`.

If the application has TCP networking (`CONFIG_NETWORKING` and `CONFIG_NET_TCP`), that's enough. Tracegale adds its HTTPS and TLS code, with
TLS defaults you can override, and starts itself. Without networking, the build warns that the device can't report anything.

For over-the-air updates, also run `west config build.sysbuild true` once per workspace, and give the application settings
storage. See [Over-the-air updates](#over-the-air-updates).

Which boards it builds for, with the whole stack's size on each: [boards.md](boards.md).

## HTTPS or MQTT

A device reaches the server over HTTPS by default. To use MQTT instead, add `CONFIG_TRACEGALE_TRANSPORT_MQTT=y`. Everything else
is the same: the credentials, the messages, what the server does with them, and every feature. A device uses one or the other,
never both.

**The port follows the binding.** Each is on its own port: 8443 for HTTPS and 8883 for MQTT, unless the server changed them.
Provisioning without a port, `tracegale server <host>`, takes the port for the firmware's binding, from
`CONFIG_TRACEGALE_SERVER_PORT_HTTPS` or `CONFIG_TRACEGALE_SERVER_PORT_MQTT`. Set those only if your server or proxy uses other
ports. A device given the other binding's port logs a warning at `tracegale commit`, because otherwise it fails with only
`last error 3`, a connect error. `tracegale status` shows the binding.

| | HTTPS | MQTT |
|---|---|---|
| Kconfig | `CONFIG_TRACEGALE_TRANSPORT_HTTPS`, the default | `CONFIG_TRACEGALE_TRANSPORT_MQTT` |
| Server port | 8443, or 443 behind a proxy | 8883 |
| Flash, `nrf52840dk`, Zephyr v4.5 | 121,424 bytes | 112,172 bytes, about 9 KB less (see "What it costs") |
| Firewalls | Port 443 passes almost everywhere | Port 8883 is blocked on some networks |

MQTT 3.1.1 over TLS, at QoS 1. The device's identity is its certificate's common name, which the server sets when it issues the
certificate, so provisioning doesn't change. Each check-in connects, sends its messages, and disconnects, as over HTTPS.
MQTT needs no buffer the size of a message: each message is sent from, and each reply read into, the buffer Tracegale
already has. Tracegale's connection is its own, apart from any MQTT connection the application has.

**Which MQTT client.** Tracegale has two, chosen by `CONFIG_TRACEGALE_MQTT_CLIENT`:

- **Tracegale's own** (`CONFIG_TRACEGALE_MQTT_CLIENT_OWN`), the default: a small client with only what Tracegale needs.
- **Zephyr's MQTT library** (`CONFIG_TRACEGALE_MQTT_CLIENT_ZEPHYR`), chosen by default when the application turns that library
  on with `CONFIG_MQTT_LIB=y`, for its own broker. The device then carries one MQTT client, not two, maintained with Zephyr.

Measured on `nrf52840dk`, Zephyr v4.5:

| | Flash | RAM |
|---|---|---|
| Tracegale's own client | 2,246 bytes | 139 bytes |
| Zephyr's library, in an application that already has it | 1,580 bytes | 434 bytes |
| Zephyr's library, in an application that doesn't | 5,496 bytes | 434 bytes |

So for an application that already uses Zephyr's library, sharing it saves about 0.7 KB of flash and costs about 0.3 KB of
RAM; to keep the RAM instead, set `CONFIG_TRACEGALE_MQTT_CLIENT_OWN=y`. Both pass the same end-to-end checks against the real
server.

**Behind a proxy.** The device sends the server's name at the start of TLS (SNI), as TLS 1.3 expects, so a proxy that picks
the service by name, for example one that shares port 8883 between Tracegale and another broker, routes it correctly.
Tracegale turns this on in Mbed TLS (`CONFIG_MBEDTLS_SSL_SERVER_NAME_INDICATION`, or `CONFIG_MBEDTLS_SERVER_NAME_INDICATION` up to Zephyr
v4.3), at 388 to 404 bytes of flash, depending on the Zephyr version. On the nRF91 series, the modem sends it. A device provisioned with an IP address in
place of a name can't be routed by name.

`CONFIG_TRACEGALE_TRANSPORT_NONE` turns reporting off.

## Your own connection

`CONFIG_TRACEGALE_TRANSPORT_CUSTOM` is for a link Tracegale has no binding for, such as a cellular modem that runs TLS and MQTT
itself on AT commands. The application provides `tg_transport_get()`, with `open`, `exchange` and `close`. The whole contract
is in `include/tracegale/transport.h`, a public header that stays stable between releases, so the application never needs
Tracegale's internal source. In short:

- **Lifecycle:** `open`, then `exchange` for each message, then `close`. `close` follows every `open` that worked, and is never
  called after one that failed, so a failing `open` cleans up after itself. All calls come from one thread at a time.
- **Messages are binary.** The link must carry every byte value. Some modems' MQTT commands accept only UTF-8 text unless told
  otherwise; the SIMCom SIM7600's `AT+CMQTTPAYLOAD`, for example, needs `AT+CMQTTCFG="checkUTF8",0,0`.
- **Credentials:** `open` reads the server, port and credentials with `tg_store_host()`, `tg_store_port()` and
  `tg_store_credential()`, in DER form, and writes them to the modem if it keeps its own.
- **Identity:** for MQTT topics, `tg_identity_from_cert()` reads the device's identity from its certificate. A transport that
  speaks the MQTT binding also sets `CONFIG_TRACEGALE_BINDING_MQTT=y`, so the device takes the server's MQTT port.
- **Errors:** return a negative `tracegale_error`, and put your own code in `*detail`, which the shell and status show. Make it
  say which step failed: it's often the only clue in a device's log.
- **Verify the server** against the provisioned CA: its chain, signature and host name. Expiry needs the time, which a device
  without a clock doesn't have; see the header.

**A link that's usually off,** such as a modem the application powers for a moment, needs no schedule of its own:

```c
/* prj.conf: CONFIG_TRACEGALE_LINK_UP_AT_BOOT=n, so Tracegale waits to be told. */
modem_power_on();
int rc = tracegale_check_in(K_SECONDS(120)); /* one attempt, which sends what's waiting; 0 if it worked */
modem_power_off();
```

While the link is down, Tracegale makes no attempts, so none fail or count in `checkin_failures`, and `tracegale status` says
`waiting for link`. `tracegale_link_set(true)` tells it the link is up for longer, and a check-in that came due meanwhile is
made at once. `tracegale_check_in()` is in the shell too, as `tracegale checkin`.

## Links

Tracegale uses whatever link carries the application's sockets, and has no code for any one of them. CI builds it on each:

- **Ethernet:** the end-to-end tests run on emulated boards over an emulated Ethernet controller.
- **Wi-Fi:** the ESP32-C3, with Zephyr's own driver for it.
- **Cellular:** Zephyr's generic cellular modem driver (`CONFIG_MODEM_CELLULAR`) and PPP, on a board whose devicetree
  describes its modem. `samples/connect/overlay-cellular.conf` is all it adds, and names no board or modem. Set the access
  point name for your SIM with `CONFIG_MODEM_CELLULAR_APN`.

A link that comes up slowly, such as cellular, is what the [network wake](#when-the-network-comes-and-goes) is for.

## The nRF Connect SDK

Tracegale works in a workspace of Nordic's nRF Connect SDK as it does on upstream Zephyr: add the module to the SDK's
`west.yml` and `CONFIG_TRACEGALE=y` to `prj.conf`. The SDK's sysbuild, MCUboot, flash layout and security library work with
the one line unchanged, so updates are on wherever MCUboot is. Three SDK versions are checked with
`scripts/ci/ncs-check.sh`:

| SDK | Built on | Why |
|---|---|---|
| v3.4 (v3.4.2) | Zephyr v4.4 | The latest release; checked in CI and locally |
| v3.3 (v3.3.5) | Zephyr v4.3 | The release before, still patched by Nordic; checked locally |
| v2.9 (v2.9.3) | Zephyr v3.7 | The last 2.x series, which many products shipped and were certified on; checked locally |

On v2.9 the update unit tests don't run, because that SDK builds MCUboot its own way without sysbuild; updates are built
for its boards with its MCUboot like the others. SDK v2.6 and older are on Zephyr releases older than v3.7, which Tracegale
doesn't support.

**The nRF91 series** has a modem that does TCP and TLS itself, through Nordic's modem library (`CONFIG_NRF_MODEM_LIB`). There,
Tracegale adds no TLS library and needs no Zephyr TCP, and keeps the credentials in the modem's own storage, at
`CONFIG_TRACEGALE_SEC_TAG`. `samples/connect/overlay-nrf-modem.conf` shows what such an application has; it names no board.

- **Provision with the link off.** The modem refuses new credentials while it's connected, and `tracegale commit` then fails
  with error 11. With the SDK's AT shell (`CONFIG_AT_SHELL`), run `at AT+CFUN=4`, provision, `tracegale commit`, then
  `at AT+CFUN=1`. An application that provisions from its own code does it before it connects.
- **Restarts need no write.** Tracegale keeps a hash of what it wrote in settings storage, and writes to the modem again only
  when the credentials change or one is missing. Without settings storage it writes at every boot.
- **No copy of the private key stays on the device** once the modem has it: Tracegale keeps only a hash of it, in memory
  and in settings storage. So if the modem ever loses its credentials, the device can't put them back, and has to be
  provisioned again; its log says so.
- **Erase** deletes the credentials from the modem too, and fails with error 11 if the link is up.
- **Cost on `nrf9151dk`:** 40,412 bytes of flash and 19,170 bytes of RAM over HTTPS, 31,208 and 18,793 over MQTT, of which
  4 KB of RAM is the SDK's own buffer for writing to the modem.
- **Turn off Zephyr's network stack** (`CONFIG_NET_NATIVE=n`), as the overlay does. The modem doesn't use it, and it costs
  about 56 KB of flash.

Other modems that offload sockets aren't supported yet: Tracegale's bindings need Zephyr's TCP or Nordic's modem library.

## When the network comes and goes

Tracegale checks in every `CONFIG_TRACEGALE_CHECKIN_INTERVAL_S` (an hour by default). After a failure it waits 5 seconds, then twice as
long each time, up to `CONFIG_TRACEGALE_BACKOFF_MAX_S` (an hour), with a random part so a fleet doesn't retry in step.

If the application uses Zephyr's connection manager (`CONFIG_NET_CONNECTION_MANAGER`), Tracegale also listens for the network coming up,
on any link: Ethernet, Wi-Fi or a cellular modem. After a failure, it then tries again within a random wait of up to
`CONFIG_TRACEGALE_NET_WAKE_JITTER_S` (30 seconds), instead of waiting out the backoff. That matters on links that take a while to come up,
such as cellular, and after an outage, so a crash is reported soon after the device is back. A device that's already connected keeps its
schedule, so a link that comes and goes doesn't cause extra check-ins. The random wait keeps devices that regain the network together
from reaching the server at the same moment (threat T11). Turn it off with `CONFIG_TRACEGALE_NET_WAKE=n`.

Without the connection manager, the application can say when its link is up or down with `tracegale_link_set()`, and check in
on demand with `tracegale_check_in()`, which waits for the attempt and returns its result. See
[Your own connection](#your-own-connection).

## Enrollment

The easy way to give devices their credentials: nothing is copied per device. In the web app, **Devices**, **Enrollment keys**,
make a key for the devices' project. You get two things:

- **A build file, `tracegale.conf`,** with the server's host, its HTTPS and MQTT ports, its CA, and the key's claim
  certificate. It holds no secret, so commit it with your application.
- **A token,** `tge_` and 43 characters, shown once. It's the secret.

Build with the file, and give the devices the token in one of two ways:

```
# The token built in: nothing at all is done per device. Keep this file out of your repository.
echo 'CONFIG_TRACEGALE_ENROLL_TOKEN="tge_..."' > tracegale-token.conf
west build -b <board> <app> -- -DEXTRA_CONF_FILE="tracegale.conf;tracegale-token.conf"

# Or the token typed into each device's shell once, so the firmware image holds no secret:
west build -b <board> <app> -- -DEXTRA_CONF_FILE=tracegale.conf
uart:~$ tracegale enroll tge_...
```

From code, `tracegale_enroll(token)` does what the shell command does, for a factory test jig for example.

**What happens on first boot.** The device makes its own P-256 key pair, which never leaves it, and keeps it in settings
storage. It connects with the claim certificate, through the same mutual TLS as any device, and sends `ENROLL` with its serial
number and public key. The server answers with a certificate for that key, and the device stores it and checks in at once. It
appears under **Devices**, named after its serial number, which you can rename later. If the key holds new devices for approval,
it shows as **Waiting for approval** until someone approves it, and the device asks again every minute or so.

**The serial number** is the chip's own ID from Zephyr's `hwinfo` driver, in hexadecimal, such as the nRF52's FICR device ID. On a
board without one, the device makes a random serial the first time, and keeps it. `tracegale status` shows it.

**After an erase or a reflash** that loses settings storage, the device makes a new key and asks again. A known serial with a new
key always waits for approval, whatever the key says, so holding the token isn't enough to take over a device: the web app asks
**Approve** or **Decline the new key** on the device's page.

**What it needs:**

- `CONFIG_SETTINGS` with a backend, to keep the key and certificate. Without it the build warns, and enrollment is off.
- P-256 key generation: PSA Crypto on Zephyr from v4.0 and on the nRF Connect SDK, Mbed TLS's elliptic curves on Zephyr v3.7.
  With Tracegale's own TLS these are already there. A custom transport gets them through `CONFIG_TRACEGALE_ENROLL`.
- `CONFIG_TRACEGALE_MAX_MESSAGE` of at least `CONFIG_TRACEGALE_CRED_MAX_SIZE` plus 64, which is the default with enrollment.
- Not yet the nRF91 series' modem, which can't take new credentials while it's connected. Provision those by hand.

**Cost:** measured on `nrf52840dk` with Zephyr v4.5, with the `connect` sample: 4,588 bytes of flash and 152 bytes of RAM, for
key generation, the enrollment exchange and the chip's ID. A build without updates also grows its message buffer from 512 to 640
bytes, to fit the certificate.

**Keeping the token safe.** The token can do one thing: add new devices to its project. It can't act as a device that exists,
reach anyone's data, or offer updates. If it leaks, revoke the key on its page, which stops it at once, and make another; devices
it enrolled keep working. Built in, anyone with a firmware image has it, so for images you publish, choose **Wait for approval**
when you make the key, or give the token in the shell instead.

## Provisioning by hand

For a few devices, or a modem that can't enroll yet. Each device needs its server and its own credentials, provided at runtime.
Certificates are in DER form.

Through the shell, in pieces of up to 120 bytes before base64, because shell lines are limited to 256 characters by default and anything
longer is cut off. The web app's **Add a device** page gives these commands, with the port for the binding you choose there and
each credential's expected total:

```
tracegale cred ca clear
tracegale cred cert clear
tracegale cred key clear
tracegale server fleet.example.com 8443
tracegale cred ca <base64>
tracegale cred cert <base64>
tracegale cred key <base64>
tracegale commit
```

The first three lines start each credential again, so pasting all the commands a second time after a mistake can't double one.
Each `cred` line reports the total so far. `tracegale commit` checks the credentials before using them: each must be one whole
DER element, and the key must belong to the certificate. A piece pasted twice or left out is refused with error 12, the log
says which credential, and nothing changes.

Or from code, with `tracegale_provision_server()`, `tracegale_provision_credential()` and `tracegale_provision_commit()`.

**Where credentials are kept:** with `CONFIG_SETTINGS`, they're stored and survive a reset. Otherwise, the application provisions them at each boot.

**Protect the private key:** settings storage isn't encrypted by Tracegale, so the key sits in plain flash unless your hardware encrypts
it. Anyone with physical access could read it. That exposes only this device, since each has its own credentials (threat T5), and it can be
revoked. Shell commands can also replace the credentials, so turn `CONFIG_TRACEGALE_SHELL` off in production builds if anyone untrusted can
reach the shell.

**Random numbers:** TLS is only as safe as its random numbers. Where upstream Zephyr has no random number driver for a chip,
such as the nRF5340's application core, its Mbed TLS falls back to `TEST_RANDOM_GENERATOR`, whose numbers are predictable, or
fails to link. Tracegale then stops the build with a message, rather than ship TLS that an attacker could break. If the chip
has a random number generator whose devicetree node is off, enable it and its driver; otherwise build such a board with its
vendor's SDK (the nRF Connect SDK for the nRF5340), or provide a driver. Only a simulator or emulator may set
`CONFIG_TRACEGALE_INSECURE_RANDOM_FOR_TESTS=y`, as Tracegale's own `native_sim` and QEMU tests do. On the nRF91 series the
modem does TLS with its own random numbers, so this doesn't apply.

## Crash reports

On by default wherever Zephyr supports coredumps, which includes Arm Cortex-M and RISC-V. When the device crashes:

1. Zephyr writes a coredump: the registers and the crashing thread's stack.
2. Tracegale keeps it in RAM that a restart doesn't clear, so no flash partition is needed, along with the image's build ID.
3. The device restarts, because Zephyr's default is to halt, and a halted device never reports.
4. At its next check-in, the device sends the crash, and erases it only once the server has all of it. A dropped connection
   resumes where it stopped.
5. If that check-in fails, and the application has settings storage (`CONFIG_SETTINGS`), Tracegale also saves the crash there, so
   a power loss while the device waits for the network can't lose it. See [Keeping a crash through a power loss](#keeping-a-crash-through-a-power-loss).

The server decodes a crash with the image's ELF file, so **upload `build/zephyr/zephyr.elf` for every build you ship**. See
`docs/server/README.md`. Crashes that arrive first wait for their file.

| Option | Default | What it does |
|---|---|---|
| `CONFIG_TRACEGALE_CRASH` | `y` where supported | Crash reports |
| `CONFIG_TRACEGALE_CRASH_RAM_SIZE` | 2048 | RAM kept for a crash, used all the time. A larger crash keeps the registers and the newest part of the stack, which is usually enough |
| `CONFIG_TRACEGALE_CRASH_KEEP` | `y` with `CONFIG_SETTINGS` | Save a crash that couldn't be sent in settings storage, so it survives a power loss |
| `CONFIG_TRACEGALE_CRASH_REBOOT` | `y` | Restart after a fatal error. Turn it off if your application has its own `k_sys_fatal_error_handler()`, and restart from there. It's off by itself with the nRF Connect SDK's `CONFIG_RESET_ON_FATAL_ERROR`, whose handler restarts too |
| `CONFIG_TRACEGALE_BUILD_ID` | `y` | A GNU build ID in the image, 36 bytes of flash |
| `CONFIG_TRACEGALE_SHELL_TEST` | `n` | `tracegale test crash` and `tracegale test fault`, to check the whole path |

### Keeping a crash through a power loss

RAM is cleared when the power goes off. A device in the field can crash while its network is down, wait hours for it, and lose
power in between. So with `CONFIG_TRACEGALE_CRASH_KEEP`, on by default when the application has `CONFIG_SETTINGS`, a crash that
couldn't be sent is saved with the application's settings, as the value `tracegale/crash`. It needs no flash partition of its own
and no devicetree changes, and works on whatever settings backend the application chose.

- **When it's saved:** after the first check-in that fails with a crash waiting, from Tracegale's thread. Nothing is written while
  the device is crashing, when flash drivers can't be trusted. A device that reaches the server at its first attempt never writes.
- **When it's loaded:** at boot, if RAM holds no crash. Its checksum is checked, as the RAM copy's is, and a damaged copy is deleted
  and never sent.
- **When it's deleted:** once the server has the crash, or has refused it for good. `tracegale erase` deletes it too.
- **Two crashes:** flash keeps the oldest unsent crash and RAM the newest, and both are sent at the next check-in that works. A
  device that keeps crashing while offline therefore writes its flash once, not at every crash (threat T20).
- **Build ID:** the kept crash carries the build ID recorded when it happened, so it's still decoded with the right file after
  an update.

What to check in your application:

- **Room:** the settings storage must take one value of `CONFIG_TRACEGALE_CRASH_RAM_SIZE` + 48 bytes, 2,096 by default. Zephyr's
  NVS, ZMS and FCB backends do, given a storage partition with room, and are tested. The file backend hasn't been tried. Backends
  limited to 256-byte values, TF-M's protected storage and the retention backend, can't hold it: the save fails, a warning is
  logged, and the crash stays in RAM only.
- **Secrets:** the dump is memory, and it now sits in flash, unencrypted unless your hardware encrypts the settings partition.
  That's the same exposure as the device's private key (threat T10). Turn the option off if that isn't acceptable.
- **Not covered:** a power loss between the crash and the end of the first failed check-in, which is usually seconds. A
  bootloader or chip that clears RAM at every reset loses a crash before it can be saved at all, which the kept copy can't help.

### Using one of Zephyr's coredump backends instead

Tracegale's RAM store is chosen by default. If the application chooses one of Zephyr's own backends, for example
`CONFIG_DEBUG_COREDUMP_BACKEND_FLASH_PARTITION=y` with a `coredump_partition` in the devicetree, Tracegale's store turns off, and
Tracegale sends whatever that backend stored, and erases it once the server has it. Zephyr's flash backend writes flash while the
device is crashing, and survives any reset. Three limits apply, because Zephyr's backends record less than Tracegale's store:

- The build ID sent is the running image's, so a crash sent after an update is decoded with the wrong file.
- Two identical crashes count as one on the server.
- A dump larger than the server accepts is refused, not cut to fit.

A backend that stores nothing to read back, such as the logging backend, builds and runs, but nothing is sent.

### Limits

- **A power loss before the crash is saved** loses it: see above. With `CONFIG_SETTINGS` off, any power loss does.
- **Zephyr v3.7 doesn't record the stack pointer**, so its crashes show only the crashing function and its caller on Cortex-M, and only
  the crashing function on RISC-V. Zephyr v4.4 and later give full backtraces.
- **A crash while another waits** replaces the older one in RAM. With a copy kept in flash, the first and the newest survive, and
  those in between are lost.
- **A coredump holds memory**, which can include secrets that were on the stack. It travels over TLS, and the server shows it only to
  administrators.
- Xtensa, which includes the original ESP32, isn't decoded yet. ESP32-C3 and other RISC-V chips are.

## Over-the-air updates

On by default when the build has MCUboot with two image slots, and the application has settings storage (`CONFIG_SETTINGS`
with a backend, `CONFIG_FLASH` and `CONFIG_FLASH_MAP`). With sysbuild, Tracegale turns MCUboot on for every board that has
a `boot_partition`. The build warns when updates are off, and says why.

Tracegale only moves bytes. **MCUboot alone decides whether an image runs**, by checking its signature with a key the
server never has. A breached server can therefore offer a device a wrong image, but can't make it run one (threat T1).

### Before you ship

- **Use your own signing key, and keep it offline.** Without one, the image is signed with MCUboot's development key, which
  is public: anyone can sign firmware with it. The build warns about this. Create a key with MCUboot's `imgtool.py keygen`,
  and set `SB_CONFIG_BOOT_SIGNATURE_KEY_FILE` in `sysbuild.conf`. MCUboot, which has the public half, can't be updated over
  the air, so the key in a shipped device is there for good.
  The server refuses to take an image signed with a development key, so a release needs your own key from the start.
- **Raise the version for every release**, in the application's `VERSION` file. It ends up in the image's MCUboot header,
  which is what a device compares.
- **Upload `build/<application>/zephyr/zephyr.signed.bin`** to the server as the release. That's the image exactly as it's
  written to the second slot.
- **Check the hardware name.** A device reports `CONFIG_TRACEGALE_HARDWARE`, which is the build's board target unless you
  set it, and the server offers it only releases uploaded for exactly that name. Set your own name when two products with
  different firmware share a board target, so that neither is ever offered the other's image. That includes hardware
  revisions built for one board target that differ by a snippet, an overlay or a signing key: give each revision's build its
  own name, for example `CONFIG_TRACEGALE_HARDWARE="sensor-hub/rev-b"` in that revision's snippet.

### What happens

1. In its reply to a check-in, the server says that an update may be waiting. Only then does the device ask, so a fleet
   with nothing to install sends nothing extra.
2. The server offers an image: its size, SHA-256 and version. Before it touches flash, the device declines an image that
   doesn't fit the slot, that it's already running, or that already failed on it `CONFIG_TRACEGALE_OTA_MAX_ATTEMPTS` times.
3. The device erases the whole second slot, notes which image it's for in settings storage, and downloads it in pieces of
   `CONFIG_TRACEGALE_MAX_MESSAGE` less 25 bytes. The first piece must be the start of an MCUboot image, and not older than
   the running one, or nothing is written.
4. When the last piece is in flash, the device checks the SHA-256 of what's in the slot, asks MCUboot to try the image, and
   restarts.
5. MCUboot checks the signature and swaps the images. The new image runs **on trial**.
6. After the new image's first check-in that works, Tracegale confirms it, and it stays. That check-in shows the image can
   start, join the network and reach the server, so it can be replaced again if it turns out bad in another way.
7. If it isn't confirmed within `CONFIG_TRACEGALE_OTA_CONFIRM_TIMEOUT_S` (15 minutes), Tracegale restarts the device, and
   MCUboot brings the previous image back. Any other restart during the trial does the same.
8. The device tells the server how it ended. The outcome is kept in settings storage until the server has it.

### When something interrupts it

| What happens | What the device does |
|---|---|
| The connection drops | Continues from the same byte at its next check-in |
| The power goes off during the download | Finds its place from what's in flash. The last flash page with data is erased and downloaded again, since a write may have been cut short. At most one page and one message are sent twice |
| The power goes off during MCUboot's swap | MCUboot finishes the swap at the next start. That's MCUboot's own design |
| The download is damaged | The SHA-256 doesn't match, so MCUboot is never asked to try it. Reported as `damaged` |
| The image is signed with another key | MCUboot refuses it, and the running image stays. Reported as `not_started` |
| The new image crashes, hangs or can't reach the server | It's never confirmed, and the previous image comes back. Reported as `reverted` |
| The rollout is stopped or changed during a download | The device asks again. The same image under a new rollout continues, and another image starts over |

### Protection against a server that misbehaves

The server is trusted to choose images, not to be harmless. These limits are in the device, so they hold even if the server
is breached or has a bug:

- **No older images.** An image with a lower version than the running one is refused before anything is written, so an old
  release with a known fault can't be put back. `CONFIG_TRACEGALE_OTA_DOWNGRADE=y` allows it. The same version with other
  contents is accepted.
- **A limit for each image.** An image that failed `CONFIG_TRACEGALE_OTA_MAX_ATTEMPTS` (3) times on this device is declined
  from then on, without downloading it.
- **A wait after a failure.** After an update that failed or was refused, the device takes no other update for
  `CONFIG_TRACEGALE_OTA_HOLD_S` (an hour), twice as long after each further failure in a row, and at most a day. The time
  counts from the failure, or from the next start if the failure ended in a restart, since a device without a clock can't
  tell how long it was off. The first update that works ends it. An update costs one erase of the slot and usually a
  restart, so this bounds both. A download that's already under way isn't held back.

### Options

| Option | Default | What it does |
|---|---|---|
| `CONFIG_TRACEGALE_OTA` | `y` with MCUboot and settings storage | Over-the-air updates |
| `CONFIG_TRACEGALE_OTA_CONFIRM` | `y` | Confirm a new image after its first check-in that works. Turn it off to confirm in your own code with `boot_write_img_confirmed()`, after your own checks |
| `CONFIG_TRACEGALE_OTA_CONFIRM_TIMEOUT_S` | 900 | How long a new image has to be confirmed before the device restarts into the previous one. Allow for your slowest link. 0 never restarts |
| `CONFIG_TRACEGALE_OTA_DOWNGRADE` | `n` | Accept an image older than the running one |
| `CONFIG_TRACEGALE_OTA_MAX_ATTEMPTS` | 3 | Times one image is tried on a device |
| `CONFIG_TRACEGALE_OTA_HOLD_S` | 3600 | The wait after a failed update. 0 turns it off |
| `CONFIG_TRACEGALE_HARDWARE` | The board target, such as `nrf52840dk/nrf52840` | The name the device reports, which a release must be uploaded for. Up to 64 bytes |
| `CONFIG_TRACEGALE_MAX_MESSAGE` | 1024 with updates | Also the size of each piece of a download, and of one buffer in RAM |

### Updates and your application

- **Hold a download back** with `tracegale_update_pause(true)`, for example while the device does something that flash
  writes or network traffic mustn't disturb. A download that's under way stops within one message, and continues from the
  same place after `tracegale_update_pause(false)`.
- **Choose when to restart** by providing `tracegale_update_restart()`. Tracegale's own version restarts at once. Yours can
  finish what the device is doing, and restart later with `sys_reboot()`. Until then it's called again after every check-in.
  The new image is installed at the next restart, whatever causes it, even if the rollout was stopped in the meantime.
- **Confirm it yourself** with `CONFIG_TRACEGALE_OTA_CONFIRM=n`, if reaching the server isn't enough to call an image good.
  Call `boot_write_img_confirmed()` once your own checks pass. Tracegale reports the update as installed at the check-in
  after that, and still restarts into the previous image if the time runs out.
- **See what's happening** in `tracegale_get_status()`: `update_state`, `update_offset`, `update_total` and `update_result`.
  `tracegale status` shows the same.

```c
void tracegale_update_restart(enum tracegale_restart_reason reason)
{
	if (reason == TRACEGALE_RESTART_INSTALL && motor_is_running()) {
		/* Not now. Tracegale asks again after its next check-in. */
		return;
	}
	sys_reboot(SYS_REBOOT_WARM);
}
```

### Limits

- **Encrypted images** (`SB_CONFIG_BOOT_ENCRYPTION`) aren't supported, and turn updates off. Tracegale tells what's running
  by comparing the first slot with the image it downloaded, and an encrypted image isn't the same bytes in the two slots.
- **MCUboot modes:** the swap modes and overwrite-only work. Direct-XIP, RAM-load, single-application and firmware-updater
  modes turn updates off. Overwrite-only keeps no previous image, so there a bad image can't be reverted.
- **One image.** Only the application's image is updated: not MCUboot, and not a second image such as a network core's.
- **Full images only.** Delta updates come after v1.
- **Erasing takes time.** The whole slot is erased in one go before a download, on Tracegale's thread. On slow flash that
  takes seconds, in which the device sends nothing.
- **Flash wear:** one erase of the second slot for each attempt, and MCUboot's swap for each install. The wait after a
  failure bounds the attempts.
- **Settings storage** must take one more value of 48 bytes, `tracegale/ota`. `tracegale erase` doesn't remove it, since
  it describes the flash and not the server.
- **A device that never reaches the server after an update** goes back to the previous image only if the trial's time is
  on (`CONFIG_TRACEGALE_OTA_CONFIRM_TIMEOUT_S`), or something else restarts it.
- **The trial needs a network.** A new image that starts while the network is down for longer than the trial's time is
  reverted, although nothing is wrong with it. It's then offered again, up to the attempt limit. Choose the time for your
  link.

## Metrics

With `CONFIG_TRACEGALE=y`, each check-in also sends a report of metrics, after any crash and before any update. The server
shows them for each device and across the fleet.

### What the firmware measures by itself

| Name | What it is | Needs |
|---|---|---|
| `uptime_s` | Seconds since the device started | Nothing |
| `heap_free` | Free bytes in the heap that `k_malloc()` uses | A heap: `CONFIG_HEAP_MEM_POOL_SIZE`, or a part of Zephyr that adds to it |
| `heap_low` | The fewest free bytes that heap has had since the device started | The same |
| `stack_low` | The least unused stack of any thread, in bytes | Nothing more: Tracegale turns on what it needs |
| `cpu_load` | How busy the CPU was in the period, in thousandths | `CONFIG_THREAD_RUNTIME_STATS=y` in your application |
| `net_sent`, `net_received` | Bytes the network stack sent and received in the period | `CONFIG_NET_STATISTICS_USER_API=y` in your application |
| `checkin_failures` | Attempts that failed in the period | Nothing |
| `checkin_ms` | How long the last check-in took, from opening the connection to the server's reply | Nothing |

Tracegale doesn't turn on CPU and network statistics itself. Run-time statistics add work to every thread switch, and network
statistics belong to your network set-up. Turn them on, and Tracegale reports them.

### Your own metrics

A **gauge** is a reading, such as a battery's voltage. A report holds the latest one, and leaves the gauge out if it wasn't
set in the period. A **counter** is a total, such as how often a door opened. A report holds what was added since the last
report, and leaves it out if nothing was.

```c
#include <tracegale/tracegale.h>

TRACEGALE_METRIC_GAUGE_DEFINE(battery_mv);
TRACEGALE_METRIC_COUNTER_DEFINE(door_opened);

void on_battery_reading(int millivolts)
{
	TRACEGALE_METRIC_SET(battery_mv, millivolts);
}

void on_door(void)
{
	TRACEGALE_METRIC_ADD(door_opened, 1);
}
```

- **Names** are 1 to 32 letters, digits and underscores, and a name that's too long fails the build. Use each name once in
  your firmware. To use a metric in another file, write `TRACEGALE_METRIC_DECLARE(name);` there.
- **Values** are 32-bit signed integers. Report a fraction in smaller units, such as millivolts or tenths of a degree. A
  counter that would pass the limit stays at it.
- **Any thread or interrupt** can set or add. Each call takes a spinlock for a few instructions.
- **Each metric costs 16 bytes of RAM**, and up to 128 gauges and 128 counters are sent. A report that doesn't fit one
  message is sent in several.
- **With `CONFIG_TRACEGALE_METRICS=n`** the macros compile to nothing, so your code builds unchanged.

### Nothing is lost when a report is

A report's values are given up only when the server acknowledges them. If the connection fails, the next report holds
everything since the last one that arrived: counters add up, a gauge holds its newest reading, and the period is longer. What
your code sets or adds while a report is on its way goes into the next one.

A server that's busy or limiting the device's rate gets the values again at the next check-in. A server that refuses the report
for another reason, such as one older than metrics, would refuse it again, so the values are dropped. In both cases the
check-in counts as a success and any update goes ahead, since metrics mustn't keep a device from being updated.

### Options

| Option | Default | What it's for |
|---|---|---|
| `CONFIG_TRACEGALE_METRICS` | on | Reports of metrics |
| `CONFIG_TRACEGALE_METRICS_HEAP` | on | `heap_free` and `heap_low`. Turns on `CONFIG_SYS_HEAP_RUNTIME_STATS` |
| `CONFIG_TRACEGALE_METRICS_STACK` | on | `stack_low`. Turns on `CONFIG_INIT_STACKS`, `CONFIG_THREAD_MONITOR` and `CONFIG_THREAD_STACK_INFO` |
| `CONFIG_TRACEGALE_METRICS_STACK_LOCKED` | off | Measure the stacks with interrupts off. Needed if your application ends threads while it runs |
| `CONFIG_TRACEGALE_METRICS_CPU` | on, when available | `cpu_load` |
| `CONFIG_TRACEGALE_METRICS_NET` | on, when available | `net_sent` and `net_received` |

**Stacks are measured with interrupts on**, as Zephyr's thread analyzer does by default, because reading every stack takes
long enough to matter. A thread that ends during the measurement could then be read after it's gone. If your application
creates and ends threads while it runs, turn on `CONFIG_TRACEGALE_METRICS_STACK_LOCKED`.

**`CONFIG_INIT_STACKS`** fills every stack with a pattern when a thread starts, which takes a little time at each start.

### What metrics cost

Measured on `nrf52840dk/nrf52840` with `samples/connect`, against the same build with `CONFIG_TRACEGALE_METRICS=n`, and
tracked in `ci/size-baseline.json`:

| Zephyr | Flash | RAM |
|---|---|---|
| v4.5.0 | 2,228 bytes | 68 bytes |
| v4.4.2 | 2,248 bytes | 68 bytes |
| v3.7.2 | 2,296 bytes | 52 bytes |

What the parts cost, on v4.5.0, measured before logs shared the code that sends a message and reads its reply:

| Build | Flash | RAM |
|---|---|---|
| All of metrics | 2,304 bytes | 68 bytes |
| Without `stack_low` | 2,244 bytes | 68 bytes |
| Without `stack_low`, `heap_free` and `heap_low` | 1,744 bytes | 52 bytes |

The stack measurement costs little here because the sample's shell had already turned on what it needs. In an application
without a shell, `CONFIG_INIT_STACKS` and the thread monitor cost more, so check your own build if flash is tight. Your own metrics add 16 bytes of RAM and their
names' bytes of flash each. A report of only what the firmware measures is up to 45 bytes on the wire.

## Logs

With `CONFIG_LOG=y` in your application, Tracegale keeps what the firmware logs with Zephyr's `LOG_ERR()`, `LOG_WRN()` and
`LOG_INF()` in RAM, and sends it with each check-in, after the metrics. Records logged while the device couldn't reach the
server arrive with the next check-in that works. You keep using Zephyr's logging API, and its other backends, such as the
console, work as before.

- **Levels:** records at `CONFIG_TRACEGALE_LOGS_LEVEL` and more severe are kept, info by default. A module's own log level
  applies first, so a debug record is kept only if its module logs at debug level, and the option is 4.
- **When the buffer is full**, the oldest records go, and the next message tells the server how many were lost, which the
  server shows as a warning.
- **Text** is formatted on the device, cut at `CONFIG_TRACEGALE_LOGS_LINE_MAX` bytes, and kept to printable ASCII: any other
  byte becomes `?`. A hex dump (`LOG_HEXDUMP_*`) sends only its length, since its bytes could hold anything.
- **Time:** each record carries the device's uptime when the backend got it. In Zephyr's deferred mode that's a moment after
  the call, when the log thread hands it over. The server works out the time of day.
- **Nothing is sent twice:** each record has a number for the device's start, and the server stores a record once even when
  a reply is lost and the device sends it again.
- **Up to eight messages at each check-in**, so a device that logs without pause still gets to its update.
- **Not kept through a restart.** Records waiting in RAM are lost when the device restarts, including the ones before a crash.
  The crash report itself is kept (see "Crash reports").

> Logs reach everyone with an account on the server. Don't log secrets or personal data, as with any log.

| Option | Default | What it's for |
|---|---|---|
| `CONFIG_TRACEGALE_LOGS` | on, when the application has `CONFIG_LOG=y` | Sending logs |
| `CONFIG_TRACEGALE_LOGS_LEVEL` | 3, info | 1 error, 2 warning, 3 info, 4 debug |
| `CONFIG_TRACEGALE_LOGS_BUFFER_SIZE` | 2048 | Bytes of RAM for records waiting to be sent. Each takes its text plus about 24 bytes |
| `CONFIG_TRACEGALE_LOGS_LINE_MAX` | 128 | Longest text of a record, up to 255 |

### What logs cost

`samples/connect`, which has `CONFIG_LOG=y`, on `nrf52840dk/nrf52840`, against the same build with `CONFIG_TRACEGALE_LOGS=n`,
tracked in `ci/size-baseline.json`:

| Zephyr | Flash | RAM |
|---|---|---|
| v4.5.0 | 2,340 bytes | 2,090 bytes |
| v4.4.2 | 2,392 bytes | 2,090 bytes |
| v3.7.2 | 2,412 bytes | 2,090 bytes |

The RAM is the 2,048-byte buffer and 42 bytes of state. A smaller `CONFIG_TRACEGALE_LOGS_BUFFER_SIZE` saves the difference.

In Zephyr's immediate mode (`CONFIG_LOG_MODE_IMMEDIATE`), the backend runs where the record was logged, which can be an
interrupt. It formats the text on that stack, so allow `CONFIG_TRACEGALE_LOGS_LINE_MAX` plus about 100 bytes there.

## Debugging

| Command | What it does |
|---|---|
| `tracegale status` | Shows your firmware's version and Tracegale's, the state, server, counts, last error, last message ID, stack left, reset reason, any crash waiting to be sent, what an update is doing and how the last one ended, the reports of metrics and log records sent, and the build ID |
| `tracegale metrics` | Shows what the next report of metrics would hold, without sending it |
| `tracegale logs` | Shows the log records waiting to be sent, and how many were dropped |
| `tracegale connect` | Attempts now |
| `tracegale erase` | Forgets the server and credentials |

Failures are listed in [errors.md](errors.md). The last message ID also appears in the server's records, so one ID traces a message from device to
server. `tools/wire/tracegale_wire.py` decodes a message offline.

## What it costs

### Flash and RAM

`scripts/ci/firmware-check.sh` builds `samples/connect` for `nrf52840dk/nrf52840` twice, with `CONFIG_TRACEGALE=y` and with
`CONFIG_TRACEGALE=n`, and the difference is Tracegale's cost. The sample has networking but no TLS of its own. CI fails if the
difference grows beyond the baseline in `ci/size-baseline.json` by more than 32 bytes, so growth is always a deliberate change.
The 32 bytes allow for different machines measuring the same source a few bytes apart, as build paths shift alignment.

| Zephyr | Flash | RAM |
|---|---|---|
| v4.5.0 | 121,424 bytes | 48,133 bytes |
| v4.4.2 | 117,932 bytes | 48,421 bytes |
| v3.7.2 | 118,428 bytes | 46,743 bytes |

The same build over MQTT (`CONFIG_TRACEGALE_TRANSPORT_MQTT=y`), which has no HTTP client:

| Zephyr | Flash | RAM |
|---|---|---|
| v4.5.0 | 112,172 bytes | 47,756 bytes |
| v4.4.2 | 108,800 bytes | 48,044 bytes |
| v3.7.2 | 109,960 bytes | 46,366 bytes |

These include metrics, which `CONFIG_TRACEGALE=y` turns on, at about 2.2 KB of flash and 68 bytes of RAM, and logs, which the
sample's `CONFIG_LOG=y` turns on, at about 2.4 KB of flash and 2 KB of RAM (see "What metrics cost" and "What logs cost").

Where the flash went on v4.5, measured before metrics added about 2.3 KB to Tracegale's own code:

| Part | Share | About |
|---|---|---|
| Mbed TLS: X.509, ECDSA, ECDH, AES-GCM, SHA-256 and the TLS 1.2 handshake | 61% | 71 KB |
| Zephyr's HTTP client | 9% | 10 KB |
| Tracegale's own code, including crash reports and the shell | 9% | 10 KB |
| DNS resolver | 7% | 8 KB |
| Network code: TLS sockets and TCP paths the sample didn't use | 7% | 8 KB |
| Other Zephyr code: settings, heap and helpers | 7% | 8 KB |

RAM: the Mbed TLS heap (16 KB), Tracegale's thread stack (4 KB on Arm, 6 KB elsewhere), the crash store (2 KB), the main stack raised
from 1 KB to 2 KB, the message buffer (512 bytes, or 1,024 with updates), and the rest in TLS socket state and network buffers.

**Over-the-air updates** are measured separately, because the builds above have no MCUboot. The same sample is built with
sysbuild, so with MCUboot, with and without `CONFIG_TRACEGALE_OTA`, and CI tracks the difference in the same way. It covers
the update code, Zephyr's flash stream and image manager, and the larger message buffer:

| Zephyr | Flash | RAM |
|---|---|---|
| v4.5.0 | 7,032 bytes | 641 bytes |
| v4.4.2 | 7,120 bytes | 641 bytes |
| v3.7.2 | 6,352 bytes | 633 bytes |

MCUboot itself, and the second slot that holds a new image, are the larger cost of updates, and they're the same with any
update system.

**When the application already has TLS.** Measured on v4.5 by giving the "off" build the same Mbed TLS configuration (the options of
the "on" build's `.config`), Tracegale added 31,652 bytes of flash and 12,136 bytes of RAM, before crashes were kept through a
power loss. That feature adds 736 bytes of flash and 16 bytes of RAM, so it's now about 32.4 KB and 12.2 KB. The comparison
application doesn't call the DNS resolver, so one that does saves about 8 KB more.

**Options that reduce it now**, measured on v4.5:

| Option | Flash saved | RAM saved |
|---|---|---|
| `CONFIG_TRACEGALE_CRASH=n` | 5,628 bytes | 2,529 bytes |
| `CONFIG_TRACEGALE_CRASH_KEEP=n` | 736 bytes | 16 bytes |
| `CONFIG_TRACEGALE_SHELL=n` | 3,128 bytes | 0 |
| A smaller `CONFIG_TRACEGALE_CRASH_RAM_SIZE` | 0 | The same amount |

**Future reductions**, each to be measured before it's adopted: carrying the messages over MQTT on a connection the application
already has; trimming Mbed TLS further, to only what its one cipher suite needs; a smaller HTTP client; optional DNS for devices
that connect by address; and TLS offload to modems that have it. TLS with pre-shared keys was measured at about 38 KB less flash,
but nginx can't terminate TLS-PSK, so device certificates stay.

### Stack and small chips

Measured on emulators, Zephyr v4.5, with certificates. `tracegale status` shows **stack free** on your own board when the
application has `CONFIG_INIT_STACKS=y`, so check it there too.

| | Arm Cortex-M3 (`qemu_cortex_m3`) | 32-bit RISC-V (`qemu_riscv32`) |
|---|---|---|
| Default `CONFIG_TRACEGALE_STACK_SIZE` | 4,096 bytes | 6,144 bytes |
| Used by a full TLS handshake | about 2,350 bytes | about 2,570 bytes |
| Used after a crash upload as well | about 2,800 bytes | about 3,000 bytes |

- **Stack:** the default leaves about 1,300 bytes on Arm. On RISC-V, 4,096 would leave only about 1,100, so RISC-V and every
  architecture that hasn't been measured keep 6,144. Logging from Tracegale's thread added 16 bytes.
- **Main stack:** TLS needs more than Zephyr's default of 1,024 bytes at boot. Tracegale raises the default to 2,048.
- **Smallest working example:** `samples/connect/overlay-qemu.conf` connects with 64 KB of RAM and shell provisioning, but it has to trim many of
  the application's own network buffers. Plan on more than 64 KB in practice.

Crash reports add the coredump code, Tracegale's store and SHA-256 to flash, and the store's 2 KB plus 256 bytes of message buffer to RAM.
The table under "Flash and RAM" includes them, and CI measures them on every change.
