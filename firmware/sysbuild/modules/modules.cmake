# SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
# SPDX-License-Identifier: Apache-2.0

# Runs before Kconfig. Zephyr v3.7 doesn't pass APP_DIR to Kconfig, so this passes it, along with the
# application's configuration inputs, to detect.py through the environment.
if(DEFINED APP_DIR)
  # Sysbuild names the main image after the application folder, and accepts <image>_<setting> forms.
  get_filename_component(tracegale_image ${APP_DIR} NAME)

  set(ENV{TRACEGALE_APP_DIR} "${APP_DIR}")
  set(ENV{TRACEGALE_CONF_FILE} "${${tracegale_image}_CONF_FILE};${CONF_FILE}")
  set(ENV{TRACEGALE_EXTRA_CONF_FILE} "${EXTRA_CONF_FILE};${${tracegale_image}_EXTRA_CONF_FILE}")
  set(ENV{TRACEGALE_DTC_OVERLAY}
      "${DTC_OVERLAY_FILE};${${tracegale_image}_DTC_OVERLAY_FILE};${EXTRA_DTC_OVERLAY_FILE};${${tracegale_image}_EXTRA_DTC_OVERLAY_FILE}")

  if(DEFINED ${tracegale_image}_CONFIG_TRACEGALE)
    set(ENV{TRACEGALE_CONFIG_OVERRIDE} "${${tracegale_image}_CONFIG_TRACEGALE}")
  elseif(DEFINED CONFIG_TRACEGALE)
    set(ENV{TRACEGALE_CONFIG_OVERRIDE} "${CONFIG_TRACEGALE}")
  endif()
endif()
