# SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(tracegale_wire_test)

# The vectors live in one file for every implementation, so they are converted at build time, not copied.
set(vectors ${ZEPHYR_TRACEGALE_MODULE_DIR}/docs/protocol/vectors.json)
set(generated ${CMAKE_CURRENT_BINARY_DIR}/vectors.inc)
find_package(Python3 REQUIRED)
add_custom_command(OUTPUT ${generated}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/vectors_to_c.py ${vectors} ${generated}
  DEPENDS ${vectors} ${CMAKE_CURRENT_SOURCE_DIR}/vectors_to_c.py)
add_custom_target(vectors_inc DEPENDS ${generated})
add_dependencies(app vectors_inc)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${ZEPHYR_TRACEGALE_MODULE_DIR}/firmware/src
  ${ZEPHYR_TRACEGALE_MODULE_DIR}/firmware/include)
target_sources(app PRIVATE src/main.c ${ZEPHYR_TRACEGALE_MODULE_DIR}/firmware/src/wire.c
  ${ZEPHYR_TRACEGALE_MODULE_DIR}/firmware/src/sha256.c)
# The identity and DER readers are pure code, tested here although only some builds use them.
target_sources(app PRIVATE ${ZEPHYR_TRACEGALE_MODULE_DIR}/firmware/src/identity.c ${ZEPHYR_TRACEGALE_MODULE_DIR}/firmware/src/der.c)
