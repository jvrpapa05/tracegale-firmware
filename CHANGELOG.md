# Changelog

Tracegale's releases. Versions follow Semantic Versioning, and the firmware and the server share each version number. Through
every 1.x release the device protocol, the firmware's public API and Kconfig options, the admin API, upgrades and backups stay
compatible.

## 0.9.0

The first public release, ahead of 1.0.

### Firmware (a Zephyr module, Apache-2.0)

- **One line to turn on:** `CONFIG_TRACEGALE=y`. It uses the application's own network, never turns networking on, and is
  given its server and credentials at runtime, never at build time.
- **Crash reports:** the crashing thread's stack and registers, kept in RAM through the reset, and in settings storage if the
  device can't report at once, sent at the next check-in, on Arm Cortex-M and RISC-V.
- **Over-the-air updates** through MCUboot, which sysbuild sets up with ECDSA signatures by itself: resumable downloads,
  downgrades refused, development keys refused, a trial run with automatic rollback.
- **Metrics** (memory, stacks, CPU, data use, check-in time, and the application's own) and **logs** from Zephyr's logging,
  sent with check-ins and kept while offline.
- **HTTPS or MQTT**, over Ethernet, Wi-Fi or cellular, through Zephyr's own drivers, or Nordic's modem library on the nRF91
  series, whose modem does TLS and keeps the credentials.
- **Supported:** Zephyr v4.5, v4.4 and v3.7 (LTS), and the nRF Connect SDK v3.4.
- A shell to provision and inspect a device, and a documented code for every error.

### Tested

The firmware's parsers of what a server sends were fuzzed with sanitizers, about 530 million inputs without a crash; the
reports are in [tracegale-server's docs/reports/](https://github.com/jvrpapa05/tracegale-server/tree/main/docs/reports).

### Known limits

- Not yet run on real hardware; tested on emulators and in builds for real boards.
- Crash decoding on Arm Cortex-M and RISC-V only; Xtensa crashes are captured but not yet decoded.
- On Zephyr v3.7, backtraces show only the crashing function and its caller.
- Device certificates can't be rotated remotely yet.
