# SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
# Zephyr v4.5 puts the application's git description in its boot banner, which differs between a git checkout and a copy, so
# CI and local builds had different sizes. A fixed one keeps the sample's image, and the size check, the same everywhere.
set(APP_BUILD_VERSION "sample")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(tracegale_connect)

target_sources(app PRIVATE src/main.c)
