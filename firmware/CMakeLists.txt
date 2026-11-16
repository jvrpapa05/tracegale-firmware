# SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_TRACEGALE)
  zephyr_include_directories(include)

  zephyr_library()
  zephyr_library_sources(src/version.c)
  zephyr_library_sources_ifdef(CONFIG_TRACEGALE_CORE src/core.c src/store.c src/wire.c)
  # core.c reports the application's version from this generated header, so it must exist first.
  if(TARGET app_version_h)
    add_dependencies(${ZEPHYR_CURRENT_LIBRARY} app_version_h)
  endif()
  zephyr_library_sources_ifdef(CONFIG_TRACEGALE_RESET_REASON src/reset.c)
  zephyr_library_sources_ifdef(CONFIG_TRACEGALE_SHELL src/shell.c)
  zephyr_library_sources_ifdef(CONFIG_TRACEGALE_TRANSPORT_HTTPS src/transport_https.c)
  zephyr_library_sources_ifdef(CONFIG_TRACEGALE_TRANSPORT_MQTT src/identity.c)
  zephyr_library_sources_ifdef(CONFIG_TRACEGALE_MQTT_CLIENT_OWN src/transport_mqtt.c)
  zephyr_library_sources_ifdef(CONFIG_TRACEGALE_MQTT_CLIENT_ZEPHYR src/transport_mqtt_zephyr.c)
  # Zephyr's MQTT library opens its own TLS connection.
  if(CONFIG_TRACEGALE_TRANSPORT_HTTPS OR CONFIG_TRACEGALE_MQTT_CLIENT_OWN)
    zephyr_library_sources(src/tls_socket.c)
  endif()
  if(CONFIG_TRACEGALE_CRED_NRF_MODEM)
    zephyr_library_sources(src/cred_nrf_modem.c src/sha256.c)
  elseif(CONFIG_TRACEGALE_TLS)
    zephyr_library_sources(src/cred_tls.c)
  endif()
  zephyr_library_sources_ifdef(CONFIG_TRACEGALE_CRASH src/crash.c)
  zephyr_library_sources_ifdef(CONFIG_TRACEGALE_OTA src/ota.c)
  if((CONFIG_TRACEGALE_CRASH OR CONFIG_TRACEGALE_OTA) AND NOT CONFIG_TRACEGALE_CRED_NRF_MODEM)
    zephyr_library_sources(src/sha256.c)
  endif()
  zephyr_library_sources_ifdef(CONFIG_TRACEGALE_CRASH_RAM src/crash_ram.c)
  if(CONFIG_TRACEGALE_METRICS OR CONFIG_TRACEGALE_LOGS)
    zephyr_library_sources(src/ack.c)
  endif()
  if(CONFIG_TRACEGALE_METRICS)
    zephyr_library_sources(src/metrics.c)
    # The application's metrics are collected by the linker, so defining one needs no registration at run time.
    zephyr_linker_sources(DATA_SECTIONS metrics.ld)
    zephyr_iterable_section(NAME tracegale_metric GROUP DATA_REGION ${XIP_ALIGN_WITH_INPUT}
                            SUBALIGN ${CONFIG_LINKER_ITERABLE_SUBALIGN})
  endif()
  zephyr_library_sources_ifdef(CONFIG_TRACEGALE_LOGS src/logs.c)
  zephyr_library_sources_ifdef(CONFIG_TRACEGALE_CRASH_REBOOT src/fatal.c)

  # Zephyr links with --build-id=none, and the last option wins.
  zephyr_library_sources(src/build_id.c)
  if(CONFIG_TRACEGALE_BUILD_ID)
    zephyr_ld_options(-Wl,--build-id=sha1)
    zephyr_linker_sources(SECTIONS build_id.ld)
  else()
    zephyr_linker_sources(SECTIONS build_id_none.ld)
  endif()

  # Predictable random numbers would let an attacker recover TLS keys. See TRACEGALE_INSECURE_RANDOM_FOR_TESTS.
  if(CONFIG_TRACEGALE_TLS AND NOT CONFIG_TRACEGALE_MODEM_SOCKETS
     AND (CONFIG_TEST_RANDOM_GENERATOR OR NOT CONFIG_ENTROPY_HAS_DRIVER)
     AND NOT CONFIG_TRACEGALE_INSECURE_RANDOM_FOR_TESTS)
    message(FATAL_ERROR
      "Tracegale: this build's TLS would use Zephyr's test random number generator, whose numbers are predictable, "
      "because the board has no random number driver here. If the chip has a random number generator, enable its "
      "device in the devicetree and its driver; otherwise use the vendor's SDK or provide a driver. "
      "Only for a simulator or emulator, set CONFIG_TRACEGALE_INSECURE_RANDOM_FOR_TESTS=y.")
  endif()

  if(NOT CONFIG_TRACEGALE_CORE)
    message(WARNING
      "Tracegale: this device has no way to reach the server, so it can't report anything.\n"
      "Tracegale uses the application's connection and doesn't turn on networking by itself. "
      "Give the application TCP networking (CONFIG_NETWORKING and CONFIG_NET_TCP), or provide your own connection.")
  endif()

  if(NOT SYSBUILD)
    message(WARNING
      "Tracegale: OTA updates are off, because this build doesn't use sysbuild, so it has no bootloader.\n"
      "Run this once per workspace: west config build.sysbuild true")
  elseif(CONFIG_BOOTLOADER_MCUBOOT AND NOT CONFIG_TRACEGALE_OTA AND
         NOT (CONFIG_SETTINGS AND CONFIG_FLASH AND CONFIG_FLASH_MAP))
    # Not shown to an application that has all of this and turned updates off itself.
    message(WARNING
      "Tracegale: OTA updates are off, although this build has MCUboot.\n"
      "An update's progress is kept in the application's settings storage, so that it survives a power loss. "
      "Give the application CONFIG_SETTINGS with a backend, CONFIG_FLASH and CONFIG_FLASH_MAP.")
  elseif(CONFIG_BOOTLOADER_MCUBOOT AND NOT CONFIG_TRACEGALE_OTA AND CONFIG_MCUBOOT_ENCRYPTION_KEY_FILE)
    message(WARNING
      "Tracegale: OTA updates are off, because this build encrypts its images, which Tracegale can't install yet.")
  endif()
endif()
