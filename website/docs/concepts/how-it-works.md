# How it works

Tracegale has two halves: a small module inside your firmware, and a server you run. The module uses your application's own
network connection, and talks to the server only when it has something to send or wants to know about an update.

- **One line turns it on.** `CONFIG_TRACEGALE=y` adds crash reports, metrics, logs and, where the board has MCUboot,
  updates. Each can be turned off.
- **Each device proves who it is** with its own certificate, made by your server when you add the device. There's no
  password shared by the fleet, so one lost device exposes only itself.
- **The server never signs firmware.** You sign images with your own key, offline, and each device's bootloader checks
  the signature before it runs anything.

<!-- include: docs/protocol/README.md#how-it-fits-together -->

## Learn more

- [The protocol](../reference/protocol/index.md): every message, in detail.
- [Firmware reference](../reference/firmware/index.md): every option, and what each costs in flash and RAM.
- [Server reference](../reference/server/index.md): settings, the API, backups and monitoring.
