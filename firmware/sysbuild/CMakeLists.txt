# SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
# SPDX-License-Identifier: Apache-2.0

if(SB_CONFIG_TRACEGALE_APP_ENABLED)
  if(NOT SB_CONFIG_BOOTLOADER_MCUBOOT)
    message(WARNING
      "Tracegale: OTA updates are off for this build, because MCUboot isn't enabled.\n"
      "Possible reasons: the board has no boot_partition, the board is listed in "
      "${CMAKE_CURRENT_LIST_DIR}/mcuboot-unsupported-boards.txt, the workspace doesn't include the "
      "mcuboot module, or sysbuild.conf turned it off.")
  elseif(SB_CONFIG_BOOT_SIGNATURE_KEY_FILE MATCHES "^${ZEPHYR_MCUBOOT_MODULE_DIR}/")
    message(WARNING
      "Tracegale: this build uses MCUboot's public development key: "
      "${SB_CONFIG_BOOT_SIGNATURE_KEY_FILE}\n"
      "Anyone can sign firmware with it. NEVER ship this. Create a private key, keep it offline, and "
      "set SB_CONFIG_BOOT_SIGNATURE_KEY_FILE in sysbuild.conf.")
  endif()
endif()
