# Two MQTT clients, measured, and devices that work behind name-routing proxies

**Headline:** a Tracegale device can now reach the server over MQTT with either Tracegale's own 2.2 KB client or Zephyr's
standard MQTT library, whichever suits the application. An application that already talks to its own broker through Zephyr's
library shares it with Tracegale, so the device carries one MQTT client, not two. Both pass the same end-to-end checks
against the real server. Devices also now send the server's name at the start of TLS, so they work behind proxies that share
one port between several services.

## What was tested

1. **Size:** what each client adds to a real application on a real Cortex-M board, from the linker's own map of the image.
2. **Behaviour:** each client, in real firmware, against the real Tracegale server and its MQTT broker in Docker: check-ins,
   metrics and logs, the broker keeping each device to its own topics, and a revoked device being refused.
3. **Server name indication (SNI):** that the firmware sends the server's name when TLS starts, and what that costs. Proxies
   such as Traefik, nginx and cloud load balancers can route by that name while passing the encrypted connection through
   untouched, which is how a public port can serve Tracegale and another broker at once.

## The setup

- Firmware: Tracegale's `connect` sample, the application the project measures every size against. Sizes on
  `nrf52840dk/nrf52840` (Arm Cortex-M4) with Zephyr v4.5.0 and its standard toolchain. Builds also checked on Zephyr v4.4.2,
  v3.7.2, and the nRF Connect SDK v3.4.2 for the nRF9151, whose modem does TLS.
- Behaviour: the same firmware built for Zephyr's `native_sim`, against Tracegale's standard Docker Compose stack.
- The test machine in [README.md](README.md).

## Results

**What each client adds** (flash and RAM, from the linker map):

| | Flash | RAM |
|---|---|---|
| Tracegale's own client | 2,246 bytes | 139 bytes |
| Zephyr's library, in an application that already has it | 1,580 bytes | 434 bytes |
| Zephyr's library, in an application that doesn't | 5,496 bytes | 434 bytes |

- For an application that already uses Zephyr's MQTT library, sharing it saves about 0.7 KB of flash and costs about 0.3 KB
  of RAM, mostly the library's own connection state. Either client can be chosen with one setting.
- For an application that doesn't, Tracegale's own client is 3.3 KB smaller, so it stays the default there.
- Neither client needs a buffer the size of a message: both send each message from, and read each reply into, the buffer
  Tracegale already has.

**Behaviour:** all 39 checks of the server's end-to-end test passed, including these 8 for MQTT, 4 per client:

| Check | Tracegale's own client | Zephyr's library |
|---|---|---|
| The real firmware checks in through the broker, and one message ID traces it to the server | Passed | Passed |
| Metrics and logs arrive as they do over HTTPS | Passed | Passed |
| A device can't publish to another device's topic | Passed | Passed |
| A revoked device is refused | Passed | Passed |

**SNI:** the firmware's own end-to-end test now checks that the test server receives the server's name ("localhost") as
the device connects: passed. It costs 388 to 404 bytes of flash, depending on the Zephyr version, and 8 bytes of RAM. Through the project's own internet-facing proxy, which
shares port 8883 by name with another broker, a connection naming Tracegale reached Tracegale's broker, and one naming
nothing got the proxy's default certificate, as it should.

## What it doesn't show

- **Simulated devices for behaviour.** The MQTT checks ran on Zephyr's simulator. A real board over MQTT, through the
  internet-facing proxy, is on the maintainers' hardware test list.
- **One board for sizes.** Other architectures and toolchains give somewhat different figures; the project's size tracking
  covers the nRF9151 and other builds on every change.
- **"Already has it" is estimated** from the linker map, as this build minus the library's own share, since an application
  that already uses the library links that share anyway.
- **SNI needs a name.** A device provisioned with an IP address in place of the server's name can't be routed by name.

## How to repeat it

```sh
# Sizes: build the connect sample for nrf52840dk/nrf52840 with each setting, then read FLASH and RAM, and each object's share
# in build/zephyr/zephyr.map
west build -b nrf52840dk/nrf52840 firmware/samples/connect -- -DCONFIG_TRACEGALE_TRANSPORT_MQTT=y
west build -b nrf52840dk/nrf52840 firmware/samples/connect -- -DCONFIG_TRACEGALE_TRANSPORT_MQTT=y -DCONFIG_MQTT_LIB=y
# Behaviour: native_sim builds of the same, then
python3 server/test/e2e_compose.py --mqtt <own client zephyr.exe> --mqtt-zephyr <Zephyr library zephyr.exe> <HTTPS zephyr.exe>
```

The project's full checks run all of this on every change. Raw results:
[data/2026-11-15-mqtt-clients/](data/2026-11-15-mqtt-clients/).
