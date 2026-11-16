# Port to a new board

<!-- order: 4 -->
<!-- summary: What a board needs for Tracegale, how to check each part, and notes for Ethernet, Wi-Fi and cellular. -->

Tracegale has no code for particular boards. A board works when Zephyr gives it four things: a network connection with TCP, a place
to keep settings, a random number generator, and enough memory. This guide checks each one.

> Tracegale is tested on emulators, and built on every change for boards from Nordic, STMicroelectronics, NXP and Espressif. It
> hasn't run on real boards yet. Tell us what you find on yours.

## 1. Networking

Start from a Zephyr sample that connects on your board, such as `samples/net/sockets/dumb_http_server` for Ethernet, or your own
application. If it reaches the network, Tracegale will too. It needs:

```
CONFIG_NETWORKING=y
CONFIG_NET_TCP=y
CONFIG_NET_SOCKETS=y
CONFIG_DNS_RESOLVER=y
```

Tracegale waits for the network, and backs off and retries when it's down, so your application can bring the link up whenever it's
ready.

| Link | What your application sets up |
|---|---|
| **Ethernet** | The board's Ethernet driver, and an address: `CONFIG_NET_DHCPV4=y`, or a fixed one with `CONFIG_NET_CONFIG_SETTINGS` |
| **Wi-Fi** | The Wi-Fi driver, and connecting to the network with Zephyr's Wi-Fi management calls, with your credentials |
| **Cellular** | Zephyr's cellular modem driver with PPP (`CONFIG_MODEM_CELLULAR`, `CONFIG_NET_L2_PPP`), the modem in the devicetree, and turning the modem on |

If the build warns "this device has no way to reach the server", the application has no TCP networking yet.

## 2. Settings storage

Credentials are kept with Zephyr's settings, on a flash partition. Check your board's devicetree has one:

```
grep -r storage_partition boards/<vendor>/<board>/
```

Then turn on settings with a backend, as in [Add Tracegale to your firmware](03-firmware.md). Without storage, the device forgets its
credentials at each restart, and your code must provide them again at each boot.

The same storage keeps a crash that couldn't be sent, so it needs room for one value of `CONFIG_TRACEGALE_CRASH_RAM_SIZE` + 48
bytes, 2,096 by default, besides the credentials, which take about 1 KB.

## 3. Random numbers

TLS needs a hardware random number generator. Most chips have one, used by `CONFIG_ENTROPY_GENERATOR=y`.

If the build stops with **"this build's TLS would use Zephyr's test random number generator"**, upstream Zephyr has no random
number driver turned on for your chip. Tracegale refuses rather than ship TLS an attacker could break. If the chip has a
generator, enable its node in your board's devicetree (often `status = "okay"` on its `rng` or `trng` node) and its driver.
Otherwise build with your chip vendor's SDK, which usually has one, or provide a driver.

> `CONFIG_TEST_RANDOM_GENERATOR` is for emulators only. Never ship it: it makes the device's TLS predictable.

## 4. Memory

Tracegale adds about **118 KB of flash and 47 KB of RAM** to an application that has networking but no TLS. That's measured on
nRF52840 by building our sample with and without `CONFIG_TRACEGALE=y`, and it includes TLS and crash reports. The figures are checked
on every change, so they can't grow unnoticed.

| Zephyr | Flash | RAM |
|---|---|---|
| v4.5 | 121,424 bytes | 48,133 bytes |
| v4.4 | 117,932 bytes | 48,421 bytes |
| v3.7 (LTS) | 118,428 bytes | 46,743 bytes |

### Why it's that size

Most of it isn't Tracegale's own code. It's the secure connection: every message to the server is encrypted, and the device and the
server each prove who they are with a certificate. That takes a full TLS library.

| Part | Flash, about | What it does |
|---|---|---|
| TLS (Mbed TLS) | 71 KB (61%) | Encryption, certificates and the handshake |
| HTTP client | 10 KB (9%) | Carries Tracegale's messages to the server |
| Tracegale's own code | 10 KB (9%) | Check-ins, credentials, crash reports and the shell commands |
| Name lookup (DNS) | 8 KB (7%) | Finds the server by its name |
| Network code | 8 KB (7%) | Zephyr's TLS sockets, and parts of TCP a simpler application doesn't use |
| Other Zephyr code | 8 KB (7%) | Settings, heap and helpers that Tracegale turns on |

And where the RAM goes:

- **16 KB** of working memory for TLS, set by `CONFIG_MBEDTLS_HEAP_SIZE`.
- **4 KB** for Tracegale's thread stack on Arm, and 6 KB on other chips, set by `CONFIG_TRACEGALE_STACK_SIZE`. On emulators, a
  check-in with a crash upload used about 2.8 KB on Arm and 3.0 KB on RISC-V.
- **2 KB** to keep a crash, set by `CONFIG_TRACEGALE_CRASH_RAM_SIZE`.
- **1 KB** more for the main stack, because TLS needs it at boot.
- **512 bytes** for messages.

The rest is TLS socket state, network buffers and other parts of Zephyr that Tracegale turns on.

### If your application already uses TLS

Then the TLS library is shared, and Tracegale adds much less: **about 32 KB of flash and 12 KB of RAM**, measured the same way with an
application that already has the same TLS. If your application also looks up names with DNS, it's about 8 KB less again.

### Making it smaller

- `CONFIG_TRACEGALE_CRASH=n` saves **5.6 KB of flash and 2.5 KB of RAM**. You lose crash reports.
- `CONFIG_TRACEGALE_CRASH_KEEP=n` saves **0.7 KB of flash**. A crash that hasn't been sent is then lost if the power goes off.
- `CONFIG_TRACEGALE_SHELL=n` saves **3.1 KB of flash**. You lose the `tracegale` shell commands, so give the device its
  credentials from code instead.
- A smaller `CONFIG_TRACEGALE_CRASH_RAM_SIZE` saves **the same amount of RAM**. Crashes are more likely to arrive cut short.

Turning off the shell is also what we recommend before shipping, if anyone untrusted can reach it.

Ways to make it smaller are planned, and each will be measured before it's adopted:

- Sending through a connection your application already has, such as MQTT, instead of a separate HTTPS one.
- Trimming the TLS library to only what the connection to the server uses.
- A smaller HTTP client, and making name lookup optional for devices that connect by address.
- Using the TLS built into a cellular or Wi-Fi modem, where there is one, so the device needs no TLS library at all.

> Pre-shared keys instead of certificates would save about 38 KB of flash, but the standard web servers Tracegale runs behind can't
> accept them, so certificates stay.

### Small chips

Your networking stack comes on top of all this. A chip with 64 KB of RAM can run Tracegale, but only after trimming network buffers:
see `firmware/samples/connect/overlay-qemu.conf` in the repository for a working example. Plan on more than 64 KB in practice.

If the device crashes in Tracegale's thread, raise `CONFIG_TRACEGALE_STACK_SIZE`. With `CONFIG_INIT_STACKS=y`, `tracegale status`
shows the **stack free** after a few check-ins.

## 5. Crash reports on your chip

| Architecture | Crashes captured | Crashes decoded |
|---|---|---|
| Arm Cortex-M | Yes | Yes |
| RISC-V, such as ESP32-C3 | Yes | Yes |
| Xtensa, such as the original ESP32 | Yes | Not yet |

On Zephyr v3.7, backtraces show only the crashing function and its caller, because v3.7 doesn't record the stack pointer. v4.4 and
later give full backtraces.

## 6. Try it end to end

1. Build with `CONFIG_TRACEGALE_SHELL_TEST=y` and flash.
2. Add the device and give it its credentials, as in [Add a device](02-add-device.md).
3. Upload the build's ELF file, as in [Upload symbol files](05-symbol-files.md).
4. On the device, run `tracegale test crash`. It restarts. Give it a minute, or run `tracegale connect`.
5. The crash appears under **Issues**, with a backtrace ending in your shell command.

Then check that a crash survives a power loss on your board:

6. Take the network away: unplug the cable, or turn off the access point or the modem.
7. Run `tracegale test crash`. After the restart, `tracegale status` shows a **crash waiting**. Wait for the check-in to fail,
   or run `tracegale connect`. The log then says `Saved the crash, so a power loss can't lose it`.
8. Switch the board off, wait ten seconds, and switch it on. `tracegale status` still shows a **crash waiting**.
9. Bring the network back. The crash appears under **Issues**.

If no crash is waiting after the restart in step 7, your chip or bootloader clears RAM at reset. Choose Zephyr's flash coredump
backend instead, as `docs/firmware/README.md` in the repository describes.

If any step fails, `tracegale status` and [When something goes wrong](12-troubleshooting.md) say why.

Next: [Upload symbol files](05-symbol-files.md).
