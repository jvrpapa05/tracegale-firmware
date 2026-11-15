# SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(tracegale_crash_test)

target_include_directories(app PRIVATE ${ZEPHYR_TRACEGALE_MODULE_DIR}/firmware/src)
target_sources(app PRIVATE src/fake.c)
target_sources_ifdef(CONFIG_TRACEGALE_CRASH_RAM app PRIVATE src/main.c)
target_sources_ifdef(CONFIG_TRACEGALE_CRASH_KEEP app PRIVATE src/keep.c)
# One of Zephyr's own coredump backends instead of Tracegale's store.
if(NOT CONFIG_TRACEGALE_CRASH_RAM)
  target_sources(app PRIVATE src/zephyr_backend.c)
endif()
