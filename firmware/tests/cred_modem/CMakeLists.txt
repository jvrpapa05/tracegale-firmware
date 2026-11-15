# SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(tracegale_cred_modem_test)

# src/modem/modem_key_mgmt.h is the test's own stand-in for the SDK's header, declaring only what the backend calls.
target_include_directories(app PRIVATE src ${ZEPHYR_TRACEGALE_MODULE_DIR}/firmware/src)
target_sources(app PRIVATE
  src/main.c
  ${ZEPHYR_TRACEGALE_MODULE_DIR}/firmware/src/cred_nrf_modem.c
  ${ZEPHYR_TRACEGALE_MODULE_DIR}/firmware/src/sha256.c)
