# Tracegale firmware

Fleet firmware management for [Zephyr RTOS](https://zephyrproject.org/): crash reports, over-the-air updates, metrics and
logs, sent to a server you host yourself. One line in `prj.conf` turns it on:

```
CONFIG_TRACEGALE=y
```

This repository is the Zephyr module that goes into your firmware. The server it reports to runs on your own machine; it
will be published as open source once it's proven on real hardware. Until then, developers and hobbyists can have early
access: open an issue titled "Early access". The [docs site](https://jvrpapa05.github.io/tracegale-firmware/) covers both: installing and using the
server, adding Tracegale to your firmware, and every option.

## Status

**Version 1.0.0-rc.2, a release candidate for 1.0.** Tested on emulators and in builds for real boards from Nordic, STMicroelectronics, NXP and
Espressif, and first run on one real board, an nRF52840 with a cellular modem; rc.2 has the fixes from that run, and enrollment, so
devices add themselves with nothing pasted. Interfaces may still change before 1.0. Built for every board Zephyr v4.5
supports: 1,642 of 1,662 board configurations build `CONFIG_TRACEGALE=y` ([board list](docs/firmware/boards.md)).

## What it does

- **Crash reports:** the crashing thread's stack and registers, kept through the reset and sent at the next check-in, on Arm
  Cortex-M and RISC-V; decoded into a backtrace on the server.
- **Over-the-air updates** through MCUboot, which sysbuild sets up with ECDSA signatures by itself: resumable downloads,
  downgrades refused, a trial run with automatic rollback.
- **Metrics** (memory, stacks, CPU, data use and your own values) and **logs** from Zephyr's logging.
- **HTTPS or MQTT** over the connection your application already has: Ethernet, Wi-Fi or cellular, or the modem of Nordic's
  nRF91 series. Tracegale never turns networking on itself.
- **Credentials given at runtime**, never compiled in: one image serves every device.

## Add it to your application

In your workspace's `west.yml`:

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

Then `west update`, add `CONFIG_TRACEGALE=y` to `prj.conf`, build, and give the device the credentials your server shows when
you add it. The full guide: [docs/firmware/README.md](docs/firmware/README.md). Error codes:
[docs/firmware/errors.md](docs/firmware/errors.md).

**Supported:** Zephyr v4.5, v4.4 and v3.7 (LTS), and the nRF Connect SDK v3.4, v3.3 and v2.9.

**What it costs:** about 118 KB of flash and 47 KB of RAM on top of an application with networking but no TLS, most of it
TLS; much less when the application already uses TLS, or on the nRF91 series, whose modem does TLS. Every feature can be
turned off.

## Build and test

On Ubuntu 24.04, with this repository cloned into a folder named `tracegale` inside an empty workspace folder:

```sh
sudo apt-get install cmake ninja-build device-tree-compiler gperf ccache gcc-multilib g++-multilib python3-venv wget xz-utils \
  clang libclang-rt-18-dev
tracegale/scripts/ci/setup-zephyr.sh v4.5.0
tracegale/scripts/ci/firmware-check.sh v4.5.0
tracegale/scripts/ci/fuzz-firmware.sh
```

The supported versions are the manifests in `ci/manifests/`.

## More

- [The protocol between device and server](docs/protocol/README.md)
- [Security policy](SECURITY.md) and [threat model](docs/security/threat-model.md)
- [Release notes](CHANGELOG.md)
- [Contributing](CONTRIBUTING.md)

## Licence

Apache-2.0, so it can go into closed products. Copyright 2026 PHITES Embedded Research and Development on Engineering.
Author: John Papa. See [AUTHORS](AUTHORS).

Tracegale is an independent project, not affiliated with the Zephyr Project or the Linux Foundation. Zephyr is a trademark of
the Linux Foundation.
