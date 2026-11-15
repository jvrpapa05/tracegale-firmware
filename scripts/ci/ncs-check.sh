#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
# SPDX-License-Identifier: Apache-2.0
#
# The firmware checks for the nRF Connect SDK, in a workspace made by setup-zephyr.sh from an ncs-* manifest. firmware-check.sh
# runs this for such tags. The SDK's Zephyr is an upstream release that the other manifests already check in full, so this checks
# what the SDK adds: its own copy of Zephyr running the tests, Nordic boards built with its sysbuild and MCUboot, and the modem
# in its nRF91 series, which does TCP and TLS itself. docs/firmware/README.md, "The nRF Connect SDK".
# Usage: scripts/ci/ncs-check.sh <ncs-tag> [--update-size-baseline]
set -euo pipefail

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
  echo "Usage: $0 <ncs-tag> [--update-size-baseline]" >&2
  exit 2
fi
tag=$1
update=()
if [ "${2:-}" = "--update-size-baseline" ]; then
  update=(--update)
elif [ -n "${2:-}" ]; then
  echo "Unknown option: $2" >&2
  exit 2
fi

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
  local board=$1 dir
  shift
  dir="$out/sysbuild/${board//\//_}"
  west build -p always --sysbuild -b "$board" -d "$dir" "$sample" -- "$@"
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

west twister \
  --testsuite-root "$repo/firmware" \
  --platform native_sim \
  --platform qemu_cortex_m3 \
  --inline-logs \
  --outdir "$out/twister"

# A device with its own connection, provisioned at runtime, against a real TLS server, on the SDK's Zephyr and Mbed TLS.
west build -p always --no-sysbuild -b native_sim -d "$out/e2e" "$sample" -- -DEXTRA_CONF_FILE=overlay-sim.conf
python3 "$repo/firmware/tests/e2e/run_e2e.py" "$out/e2e/zephyr/zephyr.exe"

# Nordic's boards, with the SDK's sysbuild, partitions and MCUboot.
check_sysbuild nrf52840dk/nrf52840
check_sysbuild nrf54l15dk/nrf54l15/cpuapp
west build -p always --no-sysbuild -b nrf5340dk/nrf5340/cpuapp -d "$out/family/nrf5340dk" "$sample"
expect_on "$out/family/nrf5340dk/zephyr/.config" nrf5340dk TRACEGALE_TRANSPORT_HTTPS TRACEGALE_CRASH

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
west build -p always --no-sysbuild -b "$modem" -d "$out/size/off" "$sample" -- \
  -DEXTRA_CONF_FILE=overlay-nrf-modem.conf -DCONFIG_TRACEGALE=n
for transport in https mqtt; do
  args=(-DEXTRA_CONF_FILE=overlay-nrf-modem.conf)
  if [ "$transport" = mqtt ]; then
    args+=(-DCONFIG_TRACEGALE_TRANSPORT_MQTT=y)
  fi
  west build -p always --no-sysbuild -b "$modem" -d "$out/size/$transport" "$sample" -- "${args[@]}"
  expect_on "$out/size/$transport/zephyr/.config" "$modem" "TRACEGALE_TRANSPORT_${transport^^}"
  python3 "$repo/scripts/ci/size_report.py" \
    --baseline "$repo/ci/size-baseline.json" \
    --key "$tag/$modem/$transport" \
    --on "$out/size/$transport/zephyr/zephyr.elf" \
    --off "$out/size/off/zephyr/zephyr.elf" \
    "${update[@]}"
done
