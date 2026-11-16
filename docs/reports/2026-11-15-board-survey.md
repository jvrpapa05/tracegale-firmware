# Tracegale on every board Zephyr supports

**Headline:** Tracegale was built for every one of the 1,662 board configurations that Zephyr v4.5 supports. **1,642
(98.8%) build with the one line `CONFIG_TRACEGALE=y`.** Every board with a network interface was then built with the whole
stack, over its own link: HTTPS, crash reports, metrics, logs and updates. **Of the 213 whose network Tracegale v1 can use,
123 built: 86 over Ethernet, 33 over Wi-Fi and 4 over a cellular modem.** On 57 more, Tracegale refused on purpose: their
chips have no random number driver turned on in upstream Zephyr, so TLS would have used predictable numbers. The other 55
boards' only network is Thread, IEEE 802.15.4 or a serial link, which v1 doesn't use.
The survey also found, and fixed the same day, one security gap and two build problems in Tracegale.

## What was tested

Zephyr supports over a thousand boards from dozens of vendors. A fleet product that works only on the boards its makers
own isn't much use to them, so this survey asked two questions of every board:

1. **Does the one line build?** `samples/hello` with `CONFIG_TRACEGALE=y`, on every board configuration Zephyr v4.5 lists.
2. **Does the whole stack build?** `samples/connect`, Tracegale's reference application, on every board with a network
   interface, with its own network link turned on, and Tracegale's defaults: HTTPS to the server, crash reports, metrics,
   logs, and over-the-air updates where the board has MCUboot. Ethernet needs nothing more; a Wi-Fi board is built with
   Wi-Fi on, and a board whose network is a cellular modem with Zephyr's modem driver and PPP. A board whose only network
   is Thread, IEEE 802.15.4 or a serial link is counted as having no v1 link, because it can't reach the server without a
   border router or a host.

Building isn't running: a board that builds compiles and links with Tracegale. Tracegale runs end to end on Zephyr's
simulator, on emulated Arm and RISC-V boards, and on emulated hardware with the real MCUboot in every full check, and on
real boards in the maintainers' hardware tests.

## The setup

- `scripts/board-survey.sh` on the test machine in [README.md](README.md): Zephyr's own build tool, twister, building every
  board with all 12 threads, from a frozen copy of one Tracegale commit: `37730d8` for the one line, and `0f93a5f` for the whole stack, which was
  built again with each board's own link.
- Zephyr v4.5.0 with every module, and the Zephyr SDK 1.0.1's toolchains, verified by checksum. The Intel audio DSP
  toolchains weren't installed.
- Vendors' radio binaries weren't downloaded, so boards that need them were built without, as Zephyr allows for building.
- Boards whose flash has no partition named `storage_partition`, which the reference application keeps its settings in,
  were built with settings off. Tracegale supports that: it keeps its credentials in RAM until a restart.

## Results

| | Built | No v1 network | Too small | Errors | Total |
|---|---|---|---|---|---|
| `CONFIG_TRACEGALE=y` (`samples/hello`) | 1,642 | - | 2 | 18 | 1,662 |
| The whole stack (`samples/connect`) | 123 | 55 | 18 | 72 | 268 |

Of the 123, by link: Ethernet 86, Wi-Fi 33, cellular 4. Most of the 72 errors (57) are the boards refused for random
numbers, and 14 are emulators, where only tests may allow insecure random numbers.

**Why the rest didn't build:**

| Cause | `hello` | `connect` | Whose |
|---|---|---|---|
| No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK | 0 | 57 | No random number driver turned on upstream, so Tracegale refuses TLS that could be broken |
| Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 | 0 | 55 | The board or emulator |
| Too small for the whole stack | 2 | 18 | The board or emulator |
| Emulator: TLS needs CONFIG_TRACEGALE_INSECURE_RANDOM_FOR_TESTS=y, as Tracegale's own tests set | 0 | 14 | An emulator, where only tests may use insecure random numbers |
| Its toolchain isn't installed for the survey (audio DSPs) | 12 | 0 | The survey's setup, not the board |
| Needs the LLVM toolchain | 1 | 1 | The survey's setup, not the board |
| Needs OpenSSL's headers on the host, for TF-A's fiptool | 2 | 0 | The survey's setup, not the board |
| Its TF-M build runs imgtool from another Python than the survey's | 1 | 0 | The survey's setup, not the board |
| The board's own files turn MCUboot on, but it has no MCUboot slots | 1 | 0 | Zephyr or MCUboot upstream |
| Needs the sgdisk host tool | 1 | 0 | The survey's setup, not the board |

Every board, with its result and image size, is listed in [docs/firmware/boards.md](../firmware/boards.md).

**What the survey found in Tracegale, all fixed and re-tested the same day:**

1. **A security gap.** On chips with no random number driver turned on in upstream Zephyr, such as the nRF5340's
   application core, Zephyr's Mbed TLS quietly falls back to a test generator whose numbers are predictable, which would
   let an attacker recover TLS keys. Those boards built without a warning. Tracegale now stops such a build with a message
   saying what to do: enable the chip's generator, use the vendor's SDK, or provide a driver. Only simulator and emulator
   test builds may allow it, explicitly.
2. **16 boards with hardware ECDSA** (Silicon Labs series 2, boards with Arm's TF-M) stopped in Mbed TLS, because Zephyr
   v4.5 turns on a part of it only for software ECDSA. Tracegale now turns that part on, at no cost in size elsewhere.
3. **11 board configurations where MCUboot itself doesn't build** upstream (mostly TF-M boards) failed the one line.
   Tracegale now leaves MCUboot off on them and warns that updates are off, so the rest of Tracegale works. The nRF Connect SDK, whose MCUboot
   does build on Nordic's TF-M boards, keeps updates on.

## What it doesn't show

- **Building isn't running.** Runtime is tested on simulators and emulators in every check, and on hardware by the
  maintainers; most of these boards have never run Tracegale.
- **Upstream Zephyr only.** Vendors' SDKs, such as the nRF Connect SDK, add drivers that upstream lacks; the boards refused
  for random numbers may well build with their vendor's SDK.
- **The image sizes are whole images**, the reference application and Zephyr included; Tracegale's own share is measured
  in the firmware guide.
- **One Zephyr version.** v4.4 and v3.7 are built on a smaller set of boards in every check.

## How to repeat it

```sh
scripts/board-survey.sh v4.5.0                   # every board; about 3 hours on 12 threads
scripts/survey_report.py <workspace>/out <workspace>/zephyr docs/firmware/boards.md docs/reports/data/2026-11-15-board-survey
```

Raw results, every board with its result, cause and size: [data/2026-11-15-board-survey/](data/2026-11-15-board-survey/).
