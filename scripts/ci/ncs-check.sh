#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
# SPDX-License-Identifier: Apache-2.0
#
# The firmware checks for the nRF Connect SDK, in a workspace made by setup-zephyr.sh from an ncs-* manifest. firmware-check.sh
# runs this for such tags. The SDK's Zephyr is an upstream release that the other manifests already check in full, so this checks
# what the SDK adds: its own copy of Zephyr running the tests, Nordic boards built with its sysbuild and MCUboot, and the modem
# in its nRF91 series, which does TCP and TLS itself. docs/firmware/README.md, "The nRF Connect SDK".
# --ci, the short check GitHub runs, leaves out the nRF5340 build, which the local check still makes.
# Usage: scripts/ci/ncs-check.sh <ncs-tag> [--update-size-baseline] [--ci]
set -euo pipefail

if [ "$#" -lt 1 ]; then
  echo "Usage: $0 <ncs-tag> [--update-size-baseline] [--ci]" >&2
  exit 2
fi
tag=$1
update=()
ci=no
for option in "${@:2}"; do
  case "$option" in
    --update-size-baseline) update=(--update) ;;
    --ci) ci=yes ;;
    *)
      echo "Unknown option: $option" >&2
      exit 2
      ;;
  esac
done

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
workspace=$(dirname "$repo")
out="$workspace/out"
sample="$repo/firmware/samples/connect"

cd "$workspace"
# shellcheck disable=SC1091
source .venv/bin/activate
export ZEPHYR_BASE="$workspace/zephyr"

rm -rf "$out"

# Fails unless every named option is on in a build's configuration.
# Usage: expect_on <.config> <board> <option ...>
expect_on() {
  local config=$1 board=$2 option
  shift 2
  for option in "$@"; do
    grep -q "^CONFIG_$option=y" "$config" || {
      echo "$board: expected CONFIG_$option to be on" >&2
      return 1
    }
  done
}

# Builds the connect sample with sysbuild and checks that the one line gave MCUboot, signed images and updates.
# Usage: check_sysbuild <board> [extra CMake arguments]
check_sysbuild() {
  local board=$1 dir meta=()
  shift
  dir="$out/sysbuild/${board//\//_}"
  # The SDK's 2.x series writes build metadata with a script that crashes when a module isn't a git repository, as
  # a local check's copy of Tracegale may not be. Only the metadata file is left out; CI builds from a real checkout.
  case "$tag" in
    ncs-v2.*) meta=(-DCONFIG_BUILD_OUTPUT_META=n -Dmcuboot_CONFIG_BUILD_OUTPUT_META=n) ;;
  esac
  west build -p always --sysbuild -b "$board" -d "$dir" "$sample" -- "$@" "${meta[@]}"
  grep -q '^SB_CONFIG_BOOTLOADER_MCUBOOT=y' "$dir/zephyr/.config" || {
    echo "$board: expected MCUboot" >&2
    return 1
  }
  grep -q '^SB_CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256=y' "$dir/zephyr/.config" || {
    echo "$board: expected ECDSA P-256 signatures" >&2
    return 1
  }
  ls "$dir"/connect/zephyr/zephyr.signed.bin >/dev/null || {
    echo "$board: expected a signed image" >&2
    return 1
  }
  expect_on "$dir/connect/zephyr/.config" "$board" TRACEGALE_OTA TRACEGALE_CRASH
  echo "Sysbuild check passed for $board."
}

# The update tests turn MCUboot on with a stand-in for it on native_sim. The SDK's 2.x series, without sysbuild, then adds
# MCUboot as a "child image" whose partition manager can't lay out native_sim's flash, so there they're left out. The same
# code is unit-tested on every other workspace, Zephyr v3.7 included, and real updates are built below with the SDK's MCUboot.
exclude=()
case "$tag" in
  ncs-v2.*) exclude=(--exclude-tag ota) ;;
esac
west twister \
  --testsuite-root "$repo/firmware" \
  --platform native_sim \
  --platform qemu_cortex_m3 \
  --inline-logs \
  --outdir "$out/twister" "${exclude[@]}"

# A device with its own connection, provisioned at runtime, against a real TLS server, on the SDK's Zephyr and Mbed TLS.
# The SDK's 2.x series is on a Zephyr 3.7, whose simulator settings have other names, as for upstream v3.7.
sim_overlay=overlay-sim.conf
if grep -q '^VERSION_MAJOR = 3$' "$workspace/zephyr/VERSION"; then
  sim_overlay=overlay-sim-v3.conf
fi
west build -p always --no-sysbuild -b native_sim -d "$out/e2e" "$sample" -- -DEXTRA_CONF_FILE="$sim_overlay"
python3 "$repo/firmware/tests/e2e/run_e2e.py" "$out/e2e/zephyr/zephyr.exe"

# Nordic's boards, with the SDK's sysbuild, partitions and MCUboot.
check_sysbuild nrf52840dk/nrf52840
check_sysbuild nrf54l15dk/nrf54l15/cpuapp
if [ "$ci" = no ]; then
  west build -p always --no-sysbuild -b nrf5340dk/nrf5340/cpuapp -d "$out/family/nrf5340dk" "$sample"
  expect_on "$out/family/nrf5340dk/zephyr/.config" nrf5340dk TRACEGALE_TRANSPORT_HTTPS TRACEGALE_CRASH
fi

# The nRF91 series: the modem does TCP and TLS, so Tracegale must add no TLS library and keep its credentials in the modem.
modem=nrf9151dk/nrf9151/ns
check_sysbuild "$modem" -DEXTRA_CONF_FILE=overlay-nrf-modem.conf
config="$out/sysbuild/${modem//\//_}/connect/zephyr/.config"
expect_on "$config" "$modem" TRACEGALE_TRANSPORT_HTTPS TRACEGALE_CRED_NRF_MODEM TRACEGALE_NET_WAKE
if grep -q '^CONFIG_NET_SOCKETS_SOCKOPT_TLS=y' "$config"; then
  echo "$modem: Zephyr's TLS sockets are on, although the modem does TLS" >&2
  exit 1
fi

# What the one line costs on the modem, over HTTPS and over MQTT, against the same application without Tracegale.
# Before the SDK v3.4, its modem library needs the partition manager's pm_config.h, which only a sysbuild build makes; those
# builds use sysbuild with no bootloader, so the image measured is the same application's either way.
size_build=(--no-sysbuild)
size_extra=()
image="zephyr"
case "$tag" in
  ncs-v3.[0-3].*)
    size_build=(--sysbuild)
    size_extra=(-DSB_CONFIG_BOOTLOADER_NONE=y)
    image="connect/zephyr"
    ;;
esac
west build -p always "${size_build[@]}" -b "$modem" -d "$out/size/off" "$sample" -- \
  -DEXTRA_CONF_FILE=overlay-nrf-modem.conf -DCONFIG_TRACEGALE=n "${size_extra[@]}"
# mqtt-zephyr is MQTT with Zephyr's MQTT library, over the modem's TLS.
for transport in https mqtt mqtt-zephyr; do
  args=(-DEXTRA_CONF_FILE=overlay-nrf-modem.conf "${size_extra[@]}")
  expect=("TRACEGALE_TRANSPORT_${transport^^}")
  case "$transport" in
    mqtt) args+=(-DCONFIG_TRACEGALE_TRANSPORT_MQTT=y) ;;
    mqtt-zephyr)
      args+=(-DCONFIG_TRACEGALE_TRANSPORT_MQTT=y -DCONFIG_MQTT_LIB=y)
      expect=(TRACEGALE_TRANSPORT_MQTT TRACEGALE_MQTT_CLIENT_ZEPHYR)
      ;;
  esac
  west build -p always "${size_build[@]}" -b "$modem" -d "$out/size/$transport" "$sample" -- "${args[@]}"
  expect_on "$out/size/$transport/$image/.config" "$modem" "${expect[@]}"
  python3 "$repo/scripts/ci/size_report.py" \
    --baseline "$repo/ci/size-baseline.json" \
    --key "$tag/$modem/$transport" \
    --on "$out/size/$transport/$image/zephyr.elf" \
    --off "$out/size/off/$image/zephyr.elf" \
    "${update[@]}"
done
