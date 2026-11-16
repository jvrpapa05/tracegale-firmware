# Boards

Tracegale was built for every board Zephyr v4.5.0 supports, with `scripts/board-survey.sh`, from Tracegale commits `37730d8` (the one line) and `0f93a5f` (the whole stack), on 2026-11-16. Building isn't
running: a board listed as built compiles and links with Tracegale, and real hardware is tested separately (see
[README.md](README.md)). Report: [the board survey](../reports/2026-11-15-board-survey.md).

## Summary

| | Built | No v1 network | Too small or filtered | Errors | Total |
|---|---|---|---|---|---|
| `CONFIG_TRACEGALE=y` on every board (`samples/hello`) | 1642 | - | 2 | 18 | 1662 |
| The whole stack on every board with a network interface, with its own link (`samples/connect`) | 123 | 55 | 18 | 72 | 268 |

No v1 network: the board's only network is Thread, IEEE 802.15.4 or a serial link, which Tracegale v1 doesn't use.
Most errors are boards without a secure random number source turned on upstream, which Tracegale refuses on purpose.

## Why some don't build

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

## The whole stack, board by board

`samples/connect` with Tracegale's defaults: HTTPS, crash reports, metrics, logs and, where the board has MCUboot,
updates, built with the board's own link: Ethernet as it comes, Wi-Fi with `CONFIG_WIFI=y`, and a cellular modem
with `overlay-cellular.conf`. Sizes are the whole image, application and Zephyr included, from the linker; what Tracegale itself adds is in
[README.md](README.md), "What it costs". Boards without a `storage_partition` were built with settings off. A blank
size means the linker reported none: `native_sim` runs on the host, and a few multi-image boards report elsewhere.

| Vendor | Board | Link | Result | Flash | RAM |
|---|---|---|---|---|---|
| adafruit | `adafruit_feather_esp32c6/esp32c6/hpcore` | Wi-Fi | Built | 421,300 |  |
| adi | `adi_eval_adin1140d1z/max32690/m4` | Ethernet | Built, settings off | 280,564 | 118,232 |
| aithinker | `ai_m61_32s_kit@DEFAULT/bl618m05q2i` | Wi-Fi | Built | 317,224 | 78,988 |
| aithinker | `ai_m62_12f_kit/bl616c50q2i` | Wi-Fi | Built | 315,540 | 78,972 |
| aithinker | `ai_m64p_32s_kit/bl616cl4855` | Wi-Fi | Built | 312,208 | 78,556 |
| aithinker | `ai_wb2_12f_kit/bl602c00q2i` | Wi-Fi | Built | 312,548 | 73,804 |
| antmicro | `stm32h7_hdmi_board/stm32h747xx/m7` | Ethernet | Built | 282,408 | 75,420 |
| antmicro | `stm32h7_renode_reference_board/stm32h753xx` | Ethernet | Built | 281,748 | 75,356 |
| arduino | `arduino_opta/stm32h747xx/m7` | Ethernet | Built | 306,648 | 84,964 |
| arduino | `arduino_portenta_h7@1.0.0/stm32h747xx/m7` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| arduino | `arduino_portenta_h7@4.10.0/stm32h747xx/m7` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| arm | `mps2/an383` |  | Emulator: TLS needs CONFIG_TRACEGALE_INSECURE_RANDOM_FOR_TESTS=y, as Tracegale's own tests set |  |  |
| arm | `mps2/an385` |  | Emulator: TLS needs CONFIG_TRACEGALE_INSECURE_RANDOM_FOR_TESTS=y, as Tracegale's own tests set |  |  |
| armfly | `armfly_stm32h743xih6/stm32h743xx` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| atmel | `sam4e_xpro/sam4e16e` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| atmel | `sam_e70_xplained/same70q21` | Ethernet | Built, settings off | 256,356 | 73,692 |
| atmel | `sam_e70_xplained/same70q21b` | Ethernet | Built, settings off | 256,676 | 73,948 |
| atmel | `sam_v71_xult/samv71q21` | Ethernet | Built, settings off | 257,204 | 73,692 |
| atmel | `sam_v71_xult/samv71q21b` | Ethernet | Built, settings off | 257,524 | 73,948 |
| atmel | `same54_xpro/same54p20a` | Ethernet | Built | 261,064 | 73,436 |
| atmel | `samr21_xpro/samr21g18a` |  | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| bflb | `bl604e_iot_dvk/bl604e20q2i` | Wi-Fi | Built | 312,548 | 73,804 |
| bflb | `bl618g0/bl618m05q2i` | Wi-Fi | Built | 314,936 | 76,700 |
| contextualelectronics | `contextualelectronics_abc/nrf52840` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| digilent | `zedboard/xc7z020` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| emtrion | `emsbc_neon_cm7/stm32f769xx` | Ethernet | Built | 280,164 | 75,484 |
| enjoydigital | `litex_vexiiriscv/litex_vexiiriscv` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| enjoydigital | `litex_vexriscv/litex_vexriscv` | Ethernet | Built, settings off |  | 508,976 |
| espressif | `esp32_devkitc/esp32/procpu` | Wi-Fi | Built | 434,704 |  |
| espressif | `esp32_ethernet_kit/esp32/procpu` | Wi-Fi | Built | 516,384 |  |
| espressif | `esp32c3_devkitc/esp32c3` | Wi-Fi | Built | 355,620 |  |
| espressif | `esp32c3_devkitm/esp32c3` | Wi-Fi | Built | 355,620 |  |
| espressif | `esp32c3_rust/esp32c3` | Wi-Fi | Built | 357,348 |  |
| espressif | `esp32c5_devkitc/esp32c5/hpcore` | Wi-Fi | Built | 421,168 |  |
| espressif | `esp32c61_devkitc/esp32c61` | Wi-Fi | Built | 421,024 |  |
| espressif | `esp32c6_devkitc/esp32c6/hpcore` | Wi-Fi | Built | 420,676 |  |
| espressif | `esp32h2_devkitm/esp32h2` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| espressif | `esp32p4_function_ev_board/esp32p4/hpcore` | Wi-Fi | Built | 490,452 |  |
| espressif | `esp32p4x_function_ev_board/esp32p4/hpcore` | Wi-Fi | Built | 490,544 |  |
| espressif | `esp32s2_devkitc/esp32s2` | Wi-Fi | Built | 458,800 |  |
| espressif | `esp32s2_saola/esp32s2` | Wi-Fi | Built | 458,800 |  |
| espressif | `esp32s3_devkitc/esp32s3/procpu` | Wi-Fi | Built | 419,780 |  |
| espressif | `esp32s3_eye/esp32s3/procpu` | Wi-Fi | Built | 423,268 |  |
| espressif | `esp8684_devkitm/esp32c2` | Wi-Fi | Built | 355,396 |  |
| espressif | `esp_threadbr/esp32s3/procpu/ethernet` | Ethernet | Built | 421,892 |  |
| espressif | `esp_wrover_kit/esp32/procpu` | Wi-Fi | Built | 434,912 |  |
| ezurio | `bl654_sensor_board/nrf52840` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| ezurio | `bl654_usb/nrf52840` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| ezurio | `bl654_usb/nrf52840/bare` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| ezurio | `mg100/nrf52840` | Cellular | Built | 285,420 | 76,608 |
| ezurio | `pinnacle_100_dvk/nrf52840` | Cellular | Built | 285,640 |  |
| firefly | `roc_rk3588_pc/rk3588` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| firefly | `roc_rk3588_pc/rk3588/smp` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| fobe | `quill_nrf52840_mesh/nrf52840` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| gaisler | `generic_leon3/leon3` |  | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| gaisler | `gr716a_mini/gr716a` |  | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| heimann | `htpa_eval/same70q20b` | Ethernet | Built, settings off | 254,304 | 73,756 |
| heimann | `htpa_eval/same70q21` | Ethernet | Built, settings off | 253,984 | 73,500 |
| heimann | `htpa_eval/same70q21b` | Ethernet | Built, settings off | 254,304 | 73,756 |
| heltec | `heltec_t114_v2/nrf52840` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| heltec | `heltec_t114_v2/nrf52840/uf2` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| infineon | `xmc45_relax_kit/xmc4500` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| infineon | `xmc47_relax_kit/xmc4700` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| kincony | `kincony_kc868_a8/esp32/procpu` | Ethernet | Built | 502,768 |  |
| m5stack | `m5stack_nanoc6/esp32c6/hpcore` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| m5stack | `m5stack_unitc6l/esp32c6/hpcore` | Wi-Fi | Built | 421,908 |  |
| makerfabs | `matouch_mtro128g/esp32s3/procpu` | Wi-Fi | Built | 419,940 |  |
| microchip | `pic32cx_sg41_cult/pic32cx1025sg41128` | Ethernet | Built | 270,588 | 74,472 |
| microchip | `pic32cx_sg61_cult/pic32cx1025sg61128` | Ethernet | Built | 270,588 | 74,472 |
| microchip | `sam_e54_xpro/atsame54p20a` | Ethernet | Built | 271,600 | 75,420 |
| muselab | `nano_ch32v317/ch32v317` | Ethernet | Too small for the whole stack |  |  |
| muselab | `nano_ch32v317/ch32v317/ram_128k` | Ethernet | Built, settings off | 298,984 | 88,284 |
| muselab | `nano_ch32v317/ch32v317/ram_192k` | Ethernet | Built, settings off | 298,984 | 88,284 |
| muselab | `nano_ch32v317/ch32v317/ram_64k` | Ethernet | Too small for the whole stack |  |  |
| muselab | `nano_ch32v317/ch32v317/ram_96k` | Ethernet | Built, settings off | 298,984 | 88,284 |
| native | `native_sim/native` | Ethernet | Built |  |  |
| native | `native_sim/native/64` | Ethernet | Built |  |  |
| nordic | `nrf21540dk/nrf52840` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| nordic | `nrf52833dk/nrf52820` |  | Too small for the whole stack |  |  |
| nordic | `nrf52833dk/nrf52833` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| nordic | `nrf52840dk/nrf52811` |  | Too small for the whole stack |  |  |
| nordic | `nrf52840dk/nrf52840` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| nordic | `nrf52840dongle/nrf52840` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| nordic | `nrf52840dongle/nrf52840/bare` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| nordic | `nrf5340dk/nrf5340/cpuapp` |  | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| nordic | `nrf5340dk/nrf5340/cpuapp/ns` |  | Too small for the whole stack |  |  |
| nordic | `nrf7002dk/nrf5340/cpuapp` | Wi-Fi | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| nordic | `nrf7002dk/nrf5340/cpuapp/nrf7001` |  | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| nordic | `nrf9131ek/nrf9131/ns` | Cellular | Too small for the whole stack |  |  |
| nordic | `nrf9151dk/nrf9151/ns` | Cellular | Too small for the whole stack |  |  |
| nordic | `nrf9160dk@0.14.0/nrf52840` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| nordic | `nrf9160dk@0.14.0/nrf9160/ns` | Cellular | Too small for the whole stack |  |  |
| nordic | `nrf9160dk@0.7.0/nrf52840` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| nordic | `nrf9160dk@0.7.0/nrf9160/ns` | Cellular | Too small for the whole stack |  |  |
| nordic | `nrf9161dk@0.7.0/nrf9161/ns` | Cellular | Too small for the whole stack |  |  |
| nordic | `nrf9161dk@0.9.0/nrf9161/ns` | Cellular | Too small for the whole stack |  |  |
| nordic | `nrf93m1dk@0.4.0/nrf54l15/cpuapp` | Cellular | Built | 289,576 | 65,096 |
| nordic | `nrf93m1dk@0.4.0/nrf54l15/cpuapp/ns` | Cellular | Built | 108,276 | 58,076 |
| nordic | `thingy53/nrf5340/cpuapp` |  | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| nordic | `thingy53/nrf5340/cpuapp/ns` |  | Too small for the whole stack |  |  |
| nucode | `nucode_nu40/nrf52840` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| nucode | `nucode_nu40/nrf52840/bare` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| nuvoton | `numaker_m55m1/m55m1xxx` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| nuvoton | `numaker_pfm_m467/m467` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| nxp | `frdm_imxrt1186/mimxrt1186/cm33` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| nxp | `frdm_imxrt1186/mimxrt1186/cm7/extmem` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| nxp | `frdm_k64f/mk64f12` | Ethernet | Built | 264,936 | 88,668 |
| nxp | `frdm_mcxa577/mcxa577` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| nxp | `frdm_mcxa577/mcxa577/t1s` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| nxp | `frdm_mcxe31b@default/mcxe31b` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| nxp | `frdm_mcxe31b@w25q64/mcxe31b` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| nxp | `frdm_mcxn947/mcxn947/cpu0` | Ethernet | Built | 522,596 | 79,184 |
| nxp | `frdm_mcxw70/mcxw70ac` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| nxp | `frdm_mcxw71/mcxw716c` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| nxp | `frdm_mcxw72/mcxw727c` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| nxp | `frdm_rw612/rw612` | Ethernet | Built | 524,136 | 104,344 |
| nxp | `imx943_evk/mimx94398/a55` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| nxp | `imx943_evk/mimx94398/a55/smp` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| nxp | `imx943_evk/mimx94398/m33` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| nxp | `imx943_evk/mimx94398/m33/ddr` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| nxp | `imx95_evk/mimx9596/a55` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| nxp | `imx95_evk/mimx9596/m7` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| nxp | `imx95_evk/mimx9596/m7/ddr` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| nxp | `imx95_evk/mimx9596/m7/flash` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| nxp | `imx95_evk_15x15/mimx9596/m7` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| nxp | `mcx_n9xx_evk/mcxn947/cpu0` | Ethernet | Built | 521,968 | 79,064 |
| nxp | `mcxw72_evk/mcxw727c` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| nxp | `mimxrt1020_evk/mimxrt1021` | Ethernet | Built | 290,016 | 90,076 |
| nxp | `mimxrt1024_evk/mimxrt1024` | Ethernet | Built | 290,000 | 90,076 |
| nxp | `mimxrt1040_evk/mimxrt1042` | Ethernet | Built | 294,280 | 90,140 |
| nxp | `mimxrt1050_evk/mimxrt1052/hyperflash` | Ethernet | Built | 288,416 | 90,140 |
| nxp | `mimxrt1050_evk/mimxrt1052/qspi` | Ethernet | Built | 291,360 | 90,076 |
| nxp | `mimxrt1060_evk@A/mimxrt1062/qspi` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| nxp | `mimxrt1060_evk@B/mimxrt1062/qspi` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| nxp | `mimxrt1060_evk@C/mimxrt1062/hyperflash` | Ethernet | Built | 290,664 | 90,204 |
| nxp | `mimxrt1060_evk@C/mimxrt1062/qspi` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| nxp | `mimxrt1062_fmurt6/mimxrt1062` | Ethernet | Built | 291,536 | 90,396 |
| nxp | `mimxrt1064_evk/mimxrt1064` | Ethernet | Built | 295,792 | 90,332 |
| nxp | `mimxrt1160_evk/mimxrt1166/cm7` | Ethernet | Built | 302,988 | 92,060 |
| nxp | `mimxrt1170_evk@A/mimxrt1176/cm7` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| nxp | `mimxrt1170_evk@B/mimxrt1176/cm7` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| nxp | `mimxrt1180_evk/mimxrt1189/cm33` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| nxp | `mr_canhubk3/s32k344` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| nxp | `mr_canhubk3/s32k344/mcuboot` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| nxp | `mr_mcxn_t1/mcxn947/cpu0` | Ethernet | Built, settings off | 502,844 | 77,800 |
| nxp | `rd_rw612_bga/rw612` | Ethernet | Built | 507,352 | 79,280 |
| nxp | `rd_rw612_bga/rw612/ethernet` | Ethernet | Built | 524,152 | 104,408 |
| nxp | `s32z2xxdc2@B/s32z270/rtu0` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| nxp | `s32z2xxdc2@B/s32z270/rtu1` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| nxp | `s32z2xxdc2@D/s32z270/rtu0` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| nxp | `s32z2xxdc2@D/s32z270/rtu1` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| nxp | `vmu_rt1170/mimxrt1176/cm7` | Ethernet | Built | 301,108 | 92,444 |
| olimex | `esp32p4_pc/esp32p4/hpcore` | Ethernet | Built | 490,516 |  |
| olimex | `olimex_stm32_e407/stm32f407xx` | Ethernet | Built, settings off | 243,140 | 63,724 |
| others | `esp32h2_supermini/esp32h2` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| others | `jz_f407vet6/stm32f407xx` | Ethernet | Built, settings off | 248,636 | 63,852 |
| others | `promicro_nrf52840/nrf52840` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| others | `promicro_nrf52840/nrf52840/uf2` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| panasonic | `pan1770_evb/nrf52840` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| panasonic | `pan1780_evb/nrf52840` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| panasonic | `pan1783_evb/nrf5340/cpuapp` |  | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| panasonic | `pan1783a_evb/nrf5340/cpuapp` |  | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| panasonic | `pan1783a_pa_evb/nrf5340/cpuapp` |  | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| phytec | `phyboard_atlas/mimxrt1176/cm7` | Ethernet | Built | 308,016 | 106,652 |
| pine64 | `pinecone/bl602c20q2i` | Wi-Fi | Built | 312,544 | 73,804 |
| pjrc | `teensy41/mimxrt1062` | Ethernet | Built, settings off | 285,732 | 90,076 |
| qemu | `qemu_cortex_a9/xc7z007s` | Ethernet | Emulator: TLS needs CONFIG_TRACEGALE_INSECURE_RANDOM_FOR_TESTS=y, as Tracegale's own tests set |  |  |
| qemu | `qemu_cortex_m3/ti_lm3s6965` |  | Emulator: TLS needs CONFIG_TRACEGALE_INSECURE_RANDOM_FOR_TESTS=y, as Tracegale's own tests set |  |  |
| qemu | `qemu_leon3/leon3` |  | Emulator: TLS needs CONFIG_TRACEGALE_INSECURE_RANDOM_FOR_TESTS=y, as Tracegale's own tests set |  |  |
| qemu | `qemu_riscv32/qemu_virt_riscv32` | Ethernet | Emulator: TLS needs CONFIG_TRACEGALE_INSECURE_RANDOM_FOR_TESTS=y, as Tracegale's own tests set |  |  |
| qemu | `qemu_riscv32/qemu_virt_riscv32/aia-direct` |  | Emulator: TLS needs CONFIG_TRACEGALE_INSECURE_RANDOM_FOR_TESTS=y, as Tracegale's own tests set |  |  |
| qemu | `qemu_riscv32/qemu_virt_riscv32/aia-direct/smp` |  | Emulator: TLS needs CONFIG_TRACEGALE_INSECURE_RANDOM_FOR_TESTS=y, as Tracegale's own tests set |  |  |
| qemu | `qemu_riscv32/qemu_virt_riscv32/aia-imsic` |  | Emulator: TLS needs CONFIG_TRACEGALE_INSECURE_RANDOM_FOR_TESTS=y, as Tracegale's own tests set |  |  |
| qemu | `qemu_riscv32/qemu_virt_riscv32/aia-imsic/smp` |  | Emulator: TLS needs CONFIG_TRACEGALE_INSECURE_RANDOM_FOR_TESTS=y, as Tracegale's own tests set |  |  |
| qemu | `qemu_riscv32/qemu_virt_riscv32/smp` |  | Emulator: TLS needs CONFIG_TRACEGALE_INSECURE_RANDOM_FOR_TESTS=y, as Tracegale's own tests set |  |  |
| qemu | `qemu_riscv32e/qemu_virt_riscv32e` | Ethernet | Needs the LLVM toolchain |  |  |
| qemu | `qemu_riscv64/qemu_virt_riscv64` | Ethernet | Emulator: TLS needs CONFIG_TRACEGALE_INSECURE_RANDOM_FOR_TESTS=y, as Tracegale's own tests set |  |  |
| qemu | `qemu_riscv64/qemu_virt_riscv64/smp` |  | Emulator: TLS needs CONFIG_TRACEGALE_INSECURE_RANDOM_FOR_TESTS=y, as Tracegale's own tests set |  |  |
| qemu | `qemu_x86/atom` |  | Emulator: TLS needs CONFIG_TRACEGALE_INSECURE_RANDOM_FOR_TESTS=y, as Tracegale's own tests set |  |  |
| qorvo | `decawave_dwm3001cdk/nrf52833` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| rakwireless | `rak4631/nrf52840` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| raytac | `raytac_an7002q_db/nrf5340/cpuapp` |  | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| raytac | `raytac_mdbt50q_cx_40_dongle/nrf52840` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| raytac | `raytac_mdbt50q_db_33/nrf52833` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| raytac | `raytac_mdbt50q_db_40/nrf52840` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| raytac | `raytac_mdbt53_db_40/nrf5340/cpuapp` |  | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| raytac | `raytac_mdbt53_db_40/nrf5340/cpuapp/ns` |  | Too small for the whole stack |  |  |
| raytac | `raytac_mdbt53v_db_40/nrf5340/cpuapp` |  | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| raytac | `raytac_mdbt53v_db_40/nrf5340/cpuapp/ns` |  | Too small for the whole stack |  |  |
| renesas | `ek_ra8d2/r7ka8d2kflcac/cm85` | Ethernet | Built | 288,588 | 117,008 |
| renesas | `ek_ra8m2/r7ka8m2jflcac/cm85` | Ethernet | Built | 289,752 | 117,648 |
| renesas | `ek_ra8p1/r7ka8p1kflcac/cm85` | Ethernet | Built | 288,096 | 117,224 |
| renesas | `ek_ra8t2/r7ka8t2lflcac/cm85` | Ethernet | Built | 291,340 | 158,200 |
| renesas | `mck_ra8t2/r7ka8t2lflcac/cm85` | Ethernet | Built | 286,900 | 156,200 |
| ruiside | `art_pi/stm32h750xx` | Ethernet | Too small for the whole stack |  |  |
| ruiside | `ra8p1_titan/r7ka8p1kflcac/cm85` | Ethernet | Built | 292,352 | 158,384 |
| ruiside | `ra8p1_titan_mini/r7ka8p1kflcac/cm85` | Ethernet | Built | 288,928 | 117,568 |
| seeed | `reterminal_e1001/esp32s3/procpu` | Wi-Fi | Built | 419,780 |  |
| seeed | `reterminal_e1003/esp32s3/procpu` | Wi-Fi | Built | 419,780 |  |
| seeed | `xiao_ble/nrf52840` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| seeed | `xiao_ble/nrf52840/sense` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| seeed | `xiao_esp32c5/esp32c5/hpcore` | Wi-Fi | Built | 421,280 |  |
| seeed | `xiao_esp32c6/esp32c6/hpcore` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| segger | `ip_k66f/mk66f18` | Ethernet | Built | 248,580 | 85,340 |
| silabs | `sixg301_rb4407a/simg301m104lil` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| silabs | `slstk3701a/efm32gg11b820f2048gl192` | Ethernet | Built | 262,496 | 81,064 |
| silabs | `slwrb4321a/efm32gg11b820f2048gm64` | Ethernet | Built | 260,720 | 80,960 |
| silabs | `xg21_rb4195b/efr32mg21b010f1024im32` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| silabs | `xg21_rb4196b/efr32mg21b020f1024im32` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| silabs | `xg24_rb4186c/efr32mg24b210f1536im48` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| silabs | `xg24_rb4187c/efr32mg24b220f1536im48` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| silabs | `xg24_rb4188a/efr32mg24b220f1536im48` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| sipeed | `maix_m0s_dock/bl616c50q2i` | Wi-Fi | Built | 315,540 | 78,972 |
| st | `nucleo_c5a3zg/stm32c5a3xx` | Ethernet | Built | 271,984 | 124,972 |
| st | `nucleo_f207zg/stm32f207xx` | Ethernet | Built, settings off | 256,788 | 86,940 |
| st | `nucleo_f429zi/stm32f429xx` | Ethernet | Built, settings off | 259,676 | 87,388 |
| st | `nucleo_f439zi/stm32f439xx` | Ethernet | Built, settings off | 259,676 | 87,388 |
| st | `nucleo_f746zg/stm32f746xx` | Ethernet | Built | 269,080 | 74,972 |
| st | `nucleo_f756zg/stm32f756xx` | Ethernet | Built, settings off | 260,892 | 74,844 |
| st | `nucleo_f767zi/stm32f767xx` | Ethernet | Built | 268,612 | 74,972 |
| st | `nucleo_h563zi/stm32h563xx` | Ethernet | Built | 271,392 | 86,588 |
| st | `nucleo_h723zg/stm32h723xx` | Ethernet | Built, settings off | 266,268 | 74,908 |
| st | `nucleo_h743zi/stm32h743xx` | Ethernet | Built, settings off | 266,340 | 74,844 |
| st | `nucleo_h745zi_q/stm32h745xx/m4` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| st | `nucleo_h745zi_q/stm32h745xx/m7` | Ethernet | Built, settings off | 266,552 | 74,844 |
| st | `nucleo_h753zi/stm32h753xx` | Ethernet | Built | 273,952 | 75,036 |
| st | `nucleo_h755zi_q/stm32h755xx/m4` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| st | `nucleo_h755zi_q/stm32h755xx/m7` | Ethernet | Built, settings off | 266,696 | 74,844 |
| st | `nucleo_h7s3l8/stm32h7s3xx` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| st | `nucleo_h7s3l8/stm32h7s3xx/ext_flash_app` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| st | `nucleo_n657x0_q/stm32n657xx/sb` | Ethernet | Built |  | 375,172 |
| st | `stm32_micrium/stm32f107xc` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| st | `stm32f746g_disco/stm32f746xx` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| st | `stm32f7508_dk/stm32f750xx` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| st | `stm32f769i_disco/stm32f769xx` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| st | `stm32h573i_dk/stm32h573xx` | Ethernet | Built | 285,128 | 87,516 |
| st | `stm32h573i_dk/stm32h573xx/ext_flash_app` | Ethernet | Built | 293,696 | 88,156 |
| st | `stm32h573i_dk/stm32h573xx/ns` | Ethernet | Built | 48,000 | 25,776 |
| st | `stm32h5f5j_dk/stm32h5f5xx` | Ethernet | Built | 286,972 | 88,196 |
| st | `stm32h735g_disco/stm32h735xx` | Ethernet | Built, settings off | 279,684 | 75,612 |
| st | `stm32h745i_disco/stm32h745xx/m7` | Ethernet | Built | 280,468 | 75,228 |
| st | `stm32h747i_disco/stm32h747xx/m7` | Ethernet | Built |  | 75,164 |
| st | `stm32h757i_eval/stm32h757xx/m7` | Ethernet | Built | 280,536 | 75,164 |
| st | `stm32h7s78_dk/stm32h7s7xx` | Ethernet | Too small for the whole stack |  |  |
| st | `stm32h7s78_dk/stm32h7s7xx/ext_flash_app` | Ethernet | Built | 30,324 | 14,272 |
| st | `stm32mp135f_dk/stm32mp135fxx` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| st | `stm32n6570_dk/stm32n657xx/sb` | Ethernet | Built |  | 376,948 |
| telink | `tlsr9518adk80d/tlsr9518` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| ti | `cc1352p7_lp/cc1352p7` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| ti | `cc3220sf_launchxl/cc3220sf` | Wi-Fi | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| ti | `cc3235sf_launchxl/cc3235sf` | Wi-Fi | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |
| u-blox | `ubx_bmd340eval/nrf52840` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| u-blox | `ubx_bmd345eval/nrf52840` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| u-blox | `ubx_bmd380eval/nrf52840` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| u-blox | `ubx_evk_iris_w1@fidelix/rw612/ethernet` | Ethernet | Built | 523,964 | 104,344 |
| u-blox | `ubx_evkninab3/nrf52840` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| udoo | `udoo_key/esp32/procpu` | Wi-Fi | Built | 434,704 |  |
| vicharak | `shrike_fi/esp32s3/procpu` | Wi-Fi | Built | 419,812 |  |
| waveshare | `esp32p4_wifi6_dev_kit/esp32p4/hpcore` | Wi-Fi | Built | 490,656 |  |
| waveshare | `waveshare_esp32p4_eth/esp32p4/hpcore` | Ethernet | Built | 490,372 |  |
| wch | `ch32v307v_evt_r1/ch32v307` | Ethernet | Too small for the whole stack |  |  |
| wch | `ch32v307v_evt_r1/ch32v307/ram_128k` | Ethernet | Built, settings off | 299,004 | 88,284 |
| wch | `ch32v307v_evt_r1/ch32v307/ram_192k` | Ethernet | Built, settings off | 299,004 | 88,284 |
| wch | `ch32v307v_evt_r1/ch32v307/ram_64k` | Ethernet | Too small for the whole stack |  |  |
| wch | `ch32v307v_evt_r1/ch32v307/ram_96k` | Ethernet | Built, settings off | 299,004 | 88,284 |
| weact | `weact_esp32c6_mini/esp32c6/hpcore` |  | Its only network isn't Ethernet, Wi-Fi or cellular (Thread, IEEE 802.15.4 or a serial link), so it can't reach the server in v1 |  |  |
| witte | `linum/stm32h753xx` | Ethernet | Built | 275,784 | 75,228 |
| wiznet | `w6300_evb_pico2/rp2350a/m33_0` | Ethernet | No secure random numbers in upstream Zephyr: enable the chip's generator, or use the vendor's SDK |  |  |

## `CONFIG_TRACEGALE=y` on every other board

Of the 1662 board configurations, 1642 built `samples/hello`. Those that didn't are:

| Vendor | Board | Cause |
|---|---|---|
| arm | `fvp_base_revc_2xaem/v8a/smp/ns` | Needs OpenSSL's headers on the host, for TF-A's fiptool |
| arm | `fvp_base_revc_2xaem/v9a/smp/ns` | Needs OpenSSL's headers on the host, for TF-A's fiptool |
| arm | `fvp_corstone1000/a320` | Needs the sgdisk host tool |
| holyiot | `holyiot_yj16019/nrf52832` | Too small for the whole stack |
| infineon | `kit_pse84_ai/pse846gps2dbzc4a/m33/ns` | The board's own files turn MCUboot on, but it has no MCUboot slots |
| intel | `intel_adsp/ace15_mtpm` | Its toolchain isn't installed for the survey (audio DSPs) |
| intel | `intel_adsp/ace15_mtpm/sim` | Its toolchain isn't installed for the survey (audio DSPs) |
| intel | `intel_adsp/ace20_lnl` | Its toolchain isn't installed for the survey (audio DSPs) |
| intel | `intel_adsp/ace20_lnl/sim` | Its toolchain isn't installed for the survey (audio DSPs) |
| intel | `intel_adsp/ace30/ptl` | Its toolchain isn't installed for the survey (audio DSPs) |
| intel | `intel_adsp/ace30/ptl/sim` | Its toolchain isn't installed for the survey (audio DSPs) |
| intel | `intel_adsp/ace30/wcl` | Its toolchain isn't installed for the survey (audio DSPs) |
| intel | `intel_adsp/ace30/wcl/sim` | Its toolchain isn't installed for the survey (audio DSPs) |
| intel | `intel_adsp/cavs25` | Its toolchain isn't installed for the survey (audio DSPs) |
| intel | `intel_adsp/cavs25/tgph` | Its toolchain isn't installed for the survey (audio DSPs) |
| nxp | `imx8mp_evk/mimx8ml8/adsp` | Its toolchain isn't installed for the survey (audio DSPs) |
| qemu | `qemu_riscv32e/qemu_virt_riscv32e` | Needs the LLVM toolchain |
| qemu | `qemu_xtensa/sample_controller32/mpu` | Its toolchain isn't installed for the survey (audio DSPs) |
| seeed | `xiao_nrf54lm20b/nrf54lm20b/cpuapp` | Too small for the whole stack |
| st | `b_u585i_iot02a/stm32u585xx/ns` | Its TF-M build runs imgtool from another Python than the survey's |

Every board, with its result, is in [boards.csv](../reports/data/2026-11-15-board-survey/boards.csv).
