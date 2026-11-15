# SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(tracegale_ota_test)

target_include_directories(app PRIVATE ${ZEPHYR_TRACEGALE_MODULE_DIR}/firmware/src)
target_sources(app PRIVATE src/fake.c)
if(CONFIG_TRACEGALE_OTA_CONFIRM)
  target_sources(app PRIVATE src/main.c)
else()
  target_sources(app PRIVATE src/options.c)
endif()
