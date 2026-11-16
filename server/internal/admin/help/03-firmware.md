# Add Tracegale to your firmware

<!-- order: 3 -->
<!-- summary: Add the module to a Zephyr application, build it, give the device its credentials, and check it connects. -->

Tracegale's firmware is a Zephyr module. Your application needs no Tracegale code: one line in `prj.conf` turns it on. It uses the
network connection your application already has, and never turns networking on by itself.

## What you need

| Need | Why |
|---|---|
| Zephyr v4.5, v4.4 or v3.7 (LTS), or the nRF Connect SDK v3.4, v3.3 or v2.9 | The versions Tracegale is tested on |
| TCP networking: Ethernet, Wi-Fi or cellular | Tracegale reaches the server over HTTPS or MQTT, through your application's connection |
| A flash partition for settings | To keep the device's credentials across restarts. Without one, provide them from code at each boot |
| A hardware random number generator | TLS needs it |
| About 47 KB of RAM and 118 KB of flash, on top of your networking | Measured on nRF52840. Most of it is TLS. If your application already uses TLS, it's about 12 KB of RAM and 32 KB of flash. See step 4 of [Port to a new board](04-port-to-a-board.md) for where it goes and how to make it smaller |

If your board isn't one you've used with Zephyr's networking before, read [Port to a new board](04-port-to-a-board.md) first.

## 1. Add the module to your workspace

In your workspace's `west.yml`, add Tracegale's repository to `projects`:

```
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

Then fetch it:

```
west update
```

Zephyr finds the module by itself, through its `zephyr/module.yml`. Pin `revision` to a release tag or commit for your products.

## 2. Turn it on

Add one line to your application's `prj.conf`:

```
CONFIG_TRACEGALE=y
```

Your application keeps its own networking and settings. A typical application that connects over Ethernet and keeps settings in
flash already has lines like these:

```
CONFIG_NETWORKING=y
CONFIG_NET_IPV4=y
CONFIG_NET_TCP=y
CONFIG_NET_SOCKETS=y
CONFIG_DNS_RESOLVER=y
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_NVS=y
CONFIG_SETTINGS=y
CONFIG_SETTINGS_NVS=y
CONFIG_SHELL=y
```

The shell is optional. It's the easiest way to give a device its credentials, and to check on it.

### Over MQTT instead of HTTPS

Devices reach the server over HTTPS unless you choose MQTT, with one more line:

```
CONFIG_TRACEGALE_TRANSPORT_MQTT=y
```

Everything else stays the same: the credentials, and every feature. Give the device the server's MQTT port, **8883**, in step 5,
in place of 8443. MQTT costs about 9 KB less flash, but port 8883 is blocked on some networks where HTTPS passes.

A cellular link works through Zephyr's own modem driver. The `connect` sample's `overlay-cellular.conf` shows the lines an
application needs, none of them Tracegale's.

### In the nRF Connect SDK

Tracegale works in Nordic's nRF Connect SDK with the same one line, including updates through the SDK's MCUboot. On the nRF91
series, the modem does TLS itself and keeps the credentials, so:

- Provision with the modem's link off: it refuses new credentials while connected, and `tracegale commit` fails with error 11.
  With the SDK's AT shell, run `at AT+CFUN=4` first, and `at AT+CFUN=1` after `tracegale commit`.
- `tracegale erase` also needs the link off, to delete the credentials from the modem.

The `connect` sample's `overlay-nrf-modem.conf` shows the lines such an application needs, none of them Tracegale's.

## 3. Give your firmware a version

Add a `VERSION` file next to your application's `CMakeLists.txt`, so each device reports which firmware it runs:

```
VERSION_MAJOR = 1
VERSION_MINOR = 2
PATCHLEVEL = 0
VERSION_TWEAK = 0
EXTRAVERSION =
```

Without it, devices show their firmware as **unknown**.

## 4. Build and flash

```
west build -b <your board> path/to/your/app
west flash
```

For firmware updates later, build with MCUboot: run `west config build.sysbuild true` once in your workspace. Tracegale sets
MCUboot up with ECDSA signatures.

After every build you ship, upload `build/zephyr/zephyr.elf` to the server, so its crashes can be decoded. See
[Upload symbol files](05-symbol-files.md).

## 5. Give the device its credentials

Add the device on the server first, as in [Add a device](02-add-device.md). That gives you the commands to paste into the device's
shell. Each piece reports the total so far:

```
uart:~$ tracegale server tracegale.example.com 8443
uart:~$ tracegale cred cert MIIBkzCCATmgAwIBAgIQabCQpNh2QGhxx2d2f7PL5TAKBggqhkjO...
cert: 120 bytes
uart:~$ tracegale commit
provisioned
```

Or provide them from your own code, for example from a factory programming step:

```
#include <tracegale/tracegale.h>

tracegale_provision_server("tracegale.example.com", 8443);
tracegale_provision_credential(TRACEGALE_CREDENTIAL_CA, ca_der, ca_len);
tracegale_provision_credential(TRACEGALE_CREDENTIAL_DEVICE_CERT, cert_der, cert_len);
tracegale_provision_credential(TRACEGALE_CREDENTIAL_DEVICE_KEY, key_der, key_len);
tracegale_provision_commit();
```

Credentials are never compiled into the firmware, so one image serves every device.

## 6. Check it works

`tracegale status` shows the device's state. This is a real device, our sample on an emulator, after it connected:

```
uart:~$ tracegale status
firmware: 1.2.0
tracegale: 0.0.0
state: idle
server: 10.0.2.2:48359
attempts: 1
successes: 1
last error: 0, detail 0
last server error: 0
last message: 15354
next attempt: 3600 s
stack free: 1728
reset reason: 0
crash waiting: 0 bytes
crashes sent: 0
build id: 370d2c970cfe021fd7aab77ffcf07727f40d45e6
```

- **state** is `idle` between check-ins, which happen hourly. `tracegale connect` checks in now.
- **last error** explains a failure. See [When something goes wrong](12-troubleshooting.md).
- **last message** is also in the server's log, so you can find this check-in there.
- **build id** must match a symbol file on the server for crashes to be decoded.

## 7. Crash reports

Crash reports are on by default on Arm Cortex-M and RISC-V. When the device crashes, it keeps the crash in RAM, restarts, and sends
it at its next check-in. If that check-in fails, the crash is also saved in your application's settings storage, so it isn't lost
if the power goes off while the device waits for the network.

| Option | Default | When to change it |
|---|---|---|
| `CONFIG_TRACEGALE_CRASH` | `y` | Turn it off if you don't want crash reports |
| `CONFIG_TRACEGALE_CRASH_RAM_SIZE` | `2048` | Raise it if crashes arrive marked "cut to fit". It's RAM used all the time |
| `CONFIG_TRACEGALE_CRASH_KEEP` | `y` with `CONFIG_SETTINGS` | Turn it off if a crash, which holds memory, must never be written to flash |
| `CONFIG_TRACEGALE_CRASH_REBOOT` | `y` | Turn it off if your application has its own `k_sys_fatal_error_handler()`, and restart from there |
| `CONFIG_TRACEGALE_SHELL_TEST` | `n` | Turn it on to try the whole path with `tracegale test crash` |

> Without `CONFIG_SETTINGS`, a crash is kept only in RAM, and is lost if the power goes off before it's sent. The settings
> storage needs room for one value of `CONFIG_TRACEGALE_CRASH_RAM_SIZE` + 48 bytes.

## Before you ship

- Turn off `CONFIG_TRACEGALE_SHELL_TEST`, and `CONFIG_TRACEGALE_SHELL` if anyone untrusted can reach the shell, because the shell
  can replace the device's credentials.
- Upload the release build's ELF file, and keep a copy.

All the options, and what each costs, are in `docs/firmware/README.md` in the repository.

Next: [Port to a new board](04-port-to-a-board.md).
