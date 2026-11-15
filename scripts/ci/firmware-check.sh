#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
# SPDX-License-Identifier: Apache-2.0
#
# Runs the firmware tests and size check in a workspace made by setup-zephyr.sh.
# Usage: scripts/ci/firmware-check.sh <zephyr-tag> [--update-size-baseline]
set -euo pipefail

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
  echo "Usage: $0 <zephyr-tag> [--update-size-baseline]" >&2
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
# The nRF Connect SDK has checks of its own, for what it adds to the Zephyr release it's built on.
case "$tag" in
  ncs-*) exec "$repo/scripts/ci/ncs-check.sh" "$@" ;;
esac
workspace=$(dirname "$repo")
out="$workspace/out"

cd "$workspace"
# shellcheck disable=SC1091
source .venv/bin/activate
export ZEPHYR_BASE="$workspace/zephyr"

rm -rf "$out"

# Checks that one prj.conf line behaves correctly under sysbuild.
# Usage: check_sysbuild <board> <expect MCUboot: yes or no>
check_sysbuild() {
  local board=$1 expected=$2 dir config
  dir="$out/sysbuild/${board//\//_}"
  config="$dir/zephyr/.config"
  west build -p always --sysbuild -b "$board" -d "$dir" "$repo/firmware/samples/hello"

  if [ "$expected" = yes ]; then
    grep -q '^SB_CONFIG_BOOTLOADER_MCUBOOT=y' "$config" || {
      echo "$board: expected MCUboot to be on" >&2
      return 1
    }
    grep -q '^SB_CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256=y' "$config" || {
      echo "$board: expected ECDSA P-256 signatures" >&2
      return 1
    }
    ls "$dir"/hello/zephyr/zephyr.signed.* >/dev/null || {
      echo "$board: expected a signed image" >&2
      return 1
    }
  elif grep -q '^SB_CONFIG_BOOTLOADER_MCUBOOT=y' "$config"; then
    echo "$board: expected MCUboot to be off" >&2
    return 1
  fi
  echo "Sysbuild check passed for $board (MCUboot expected: $expected)."
}

# Builds the connect sample for a board of another hardware family, and checks that the one line turned everything on.
# The application here also has Zephyr's connection manager, so the network wake builds against real drivers.
# Usage: check_family <board> [extra CMake arguments]
check_family() {
  local board=$1 dir config option transport=TRACEGALE_TRANSPORT_HTTPS
  shift
  dir="$out/family/${board//\//_}"
  config="$dir/zephyr/.config"
  case " $* " in
    *" -DCONFIG_TRACEGALE_TRANSPORT_MQTT=y "*) transport=TRACEGALE_TRANSPORT_MQTT ;;
  esac
  west build -p always --no-sysbuild -b "$board" -d "$dir" "$repo/firmware/samples/connect" -- \
    -DCONFIG_NET_CONNECTION_MANAGER=y "$@"
  for option in "$transport" TRACEGALE_CRASH TRACEGALE_NET_WAKE TRACEGALE_RESET_REASON; do
    grep -q "^CONFIG_$option=y" "$config" || {
      echo "$board: expected CONFIG_$option to be on" >&2
      return 1
    }
  done
  echo "Family check passed for $board."
}

west twister \
  --testsuite-root "$repo/firmware" \
  --platform native_sim \
  --platform qemu_cortex_m3 \
  --platform qemu_riscv32 \
  --inline-logs \
  --outdir "$out/twister"

# A device with its own connection, provisioned at runtime, against a real TLS server.
case "$tag" in
  v3.*) sim_overlay=overlay-sim-v3.conf ;;
  *) sim_overlay=overlay-sim.conf ;;
esac
west build -p always --no-sysbuild -b native_sim -d "$out/e2e" "$repo/firmware/samples/connect" -- \
  -DEXTRA_CONF_FILE="$sim_overlay"
python3 "$repo/firmware/tests/e2e/run_e2e.py" "$out/e2e/zephyr/zephyr.exe"
# The same device with the MQTT binding, which the server's end-to-end test runs against the broker in its Docker stack.
west build -p always --no-sysbuild -b native_sim -d "$out/e2e-mqtt" "$repo/firmware/samples/connect" -- \
  -DEXTRA_CONF_FILE="$sim_overlay" -DCONFIG_TRACEGALE_TRANSPORT_MQTT=y
grep -q '^CONFIG_TRACEGALE_TRANSPORT_MQTT=y' "$out/e2e-mqtt/zephyr/.config" || {
  echo "expected the MQTT binding in the MQTT build" >&2
  exit 1
}

# The same on Zephyr's own TCP stack and 64 KB of RAM. This also boots with real stack sizes, which the simulator doesn't check.
# Zephyr v3.7's older TLS library doesn't fit in that RAM, so it's left out.
case "$tag" in
  v3.*) ;;
  *)
    west build -p always --no-sysbuild -b qemu_cortex_m3 -d "$out/e2e-qemu" "$repo/firmware/samples/connect" -- \
      -DEXTRA_CONF_FILE=overlay-qemu.conf
    python3 "$repo/firmware/tests/e2e/run_e2e.py" --qemu "$out/e2e-qemu/zephyr/zephyr.elf"
    ;;
esac

check_sysbuild nrf52840dk/nrf52840 yes
check_sysbuild qemu_cortex_m3 no
check_sysbuild native_sim no
# Listed in mcuboot-unsupported-boards.txt.
check_sysbuild nrf52840dongle/nrf52840 no

for board in qemu_cortex_m3 qemu_riscv32; do
  west build -p always --no-sysbuild -b "$board" -d "$out/size/$board/on" "$repo/firmware/samples/hello"
  west build -p always --no-sysbuild -b "$board" -d "$out/size/$board/off" "$repo/firmware/samples/hello" \
    -- -DCONFIG_TRACEGALE=n

  python3 "$repo/scripts/ci/size_report.py" \
    --baseline "$repo/ci/size-baseline.json" \
    --key "$tag/$board" \
    --on "$out/size/$board/on/zephyr/zephyr.elf" \
    --off "$out/size/$board/off/zephyr/zephyr.elf" \
    "${update[@]}"
done

# What the one line costs an application that already has a network stack, on real hardware's toolchain and libraries.
board=nrf52840dk/nrf52840
west build -p always --no-sysbuild -b "$board" -d "$out/size/connect/on" "$repo/firmware/samples/connect"
west build -p always --no-sysbuild -b "$board" -d "$out/size/connect/off" "$repo/firmware/samples/connect" \
  -- -DCONFIG_TRACEGALE=n
python3 "$repo/scripts/ci/size_report.py" \
  --baseline "$repo/ci/size-baseline.json" \
  --key "$tag/$board/connect" \
  --on "$out/size/connect/on/zephyr/zephyr.elf" \
  --off "$out/size/connect/off/zephyr/zephyr.elf" \
  "${update[@]}"

# The same, with the MQTT binding in place of HTTPS, so the two figures compare directly.
west build -p always --no-sysbuild -b "$board" -d "$out/size/mqtt/on" "$repo/firmware/samples/connect" \
  -- -DCONFIG_TRACEGALE_TRANSPORT_MQTT=y
grep -q '^CONFIG_TRACEGALE_TRANSPORT_MQTT=y' "$out/size/mqtt/on/zephyr/.config" || {
  echo "$board: expected the MQTT binding" >&2
  exit 1
}
python3 "$repo/scripts/ci/size_report.py" \
  --baseline "$repo/ci/size-baseline.json" \
  --key "$tag/$board/mqtt" \
  --on "$out/size/mqtt/on/zephyr/zephyr.elf" \
  --off "$out/size/connect/off/zephyr/zephyr.elf" \
  "${update[@]}"

# What metrics cost the same application: its build above has them, as the one line gives, and this one doesn't.
west build -p always --no-sysbuild -b "$board" -d "$out/size/metrics/off" "$repo/firmware/samples/connect" \
  -- -DCONFIG_TRACEGALE_METRICS=n
grep -q '^CONFIG_TRACEGALE_METRICS=y' "$out/size/connect/on/zephyr/.config" || {
  echo "$board: expected metrics to be on with CONFIG_TRACEGALE=y" >&2
  exit 1
}
python3 "$repo/scripts/ci/size_report.py" \
  --baseline "$repo/ci/size-baseline.json" \
  --key "$tag/$board/metrics" \
  --on "$out/size/connect/on/zephyr/zephyr.elf" \
  --off "$out/size/metrics/off/zephyr/zephyr.elf" \
  "${update[@]}"

# What sending logs costs the same application, which has Zephyr's logging on.
west build -p always --no-sysbuild -b "$board" -d "$out/size/logs/off" "$repo/firmware/samples/connect" \
  -- -DCONFIG_TRACEGALE_LOGS=n
grep -q '^CONFIG_TRACEGALE_LOGS=y' "$out/size/connect/on/zephyr/.config" || {
  echo "$board: expected logs to be on in an application with CONFIG_LOG=y" >&2
  exit 1
}
python3 "$repo/scripts/ci/size_report.py" \
  --baseline "$repo/ci/size-baseline.json" \
  --key "$tag/$board/logs" \
  --on "$out/size/connect/on/zephyr/zephyr.elf" \
  --off "$out/size/logs/off/zephyr/zephyr.elf" \
  "${update[@]}"

# What over-the-air updates cost, on a build that has MCUboot: the same application with and without them.
west build -p always --sysbuild -b "$board" -d "$out/size/ota/on" "$repo/firmware/samples/connect"
west build -p always --sysbuild -b "$board" -d "$out/size/ota/off" "$repo/firmware/samples/connect" \
  -- -DCONFIG_TRACEGALE_OTA=n
grep -q '^CONFIG_TRACEGALE_OTA=y' "$out/size/ota/on/connect/zephyr/.config" || {
  echo "$board: expected updates to be on in a build with MCUboot" >&2
  exit 1
}
python3 "$repo/scripts/ci/size_report.py" \
  --baseline "$repo/ci/size-baseline.json" \
  --key "$tag/$board/ota" \
  --on "$out/size/ota/on/connect/zephyr/zephyr.elf" \
  --off "$out/size/ota/off/connect/zephyr/zephyr.elf" \
  "${update[@]}"

# Builds one image of the update test. The version is the application's VERSION file, so each image gets a copy of the sample.
# Usage: build_ota_image <name> <minor version> [extra CMake arguments]
build_ota_image() {
  local name=$1 minor=$2 rig="$repo/firmware/tests/e2e/ota"
  shift 2
  mkdir -p "$out/ota"
  cp -r "$repo/firmware/samples/connect" "$out/ota/src-$name"
  sed -i "s/^VERSION_MINOR = .*/VERSION_MINOR = $minor/" "$out/ota/src-$name/VERSION"
  # The emulated board's flash isn't where its linker script expects it, which the linker's section check objects to.
  west build -p always --sysbuild -b mps2/an385 -d "$out/ota/$name" "$out/ota/src-$name" -- \
    -DEXTRA_CONF_FILE="$rig/app.conf" -DEXTRA_DTC_OVERLAY_FILE="$rig/app.overlay" \
    -DEXTRA_LDFLAGS=-Wl,--no-check-sections \
    -Dmcuboot_EXTRA_CONF_FILE="$rig/mcuboot.conf" -Dmcuboot_EXTRA_DTC_OVERLAY_FILE="$rig/mcuboot.overlay" \
    -Dmcuboot_EXTRA_LDFLAGS=-Wl,--no-check-sections "$@"
}

# The one line on unrelated hardware families: other vendors' drivers, entropy sources and links. Only the newest Zephyr
# version's manifest has these vendors' modules, which keeps the other versions' checks short.
newest=$(printf '%s\n' "$repo"/ci/manifests/west-v*.yml | sort -V | tail -n 1)
if [ "$newest" = "$repo/ci/manifests/west-$tag.yml" ]; then
  # STMicroelectronics, Arm Cortex-M7, Ethernet.
  check_family nucleo_f767zi
  # NXP, Arm Cortex-M4, Ethernet.
  check_family frdm_k64f
  # Espressif, RISC-V, Wi-Fi. Built without the vendor's radio binaries, so CI downloads none. Such an image doesn't run.
  check_family esp32c3_devkitm -DCONFIG_WIFI=y -DCONFIG_BUILD_ONLY_NO_BLOBS=y
  # Cellular, through Zephyr's generic modem driver and PPP, on a board whose own devicetree describes its modem, and over
  # MQTT, so the binding builds with a real vendor toolchain and libraries too. The overlay names no board or modem.
  check_family rak5010/nrf52840 -DEXTRA_CONF_FILE=overlay-cellular.conf -DCONFIG_TRACEGALE_TRANSPORT_MQTT=y

  # The end-to-end run on RISC-V, with Tracegale's default stack: a TLS handshake, then a real crash through a restart.
  # Zephyr's qemu_riscv32 board has a network device only from v4.5.
  west build -p always --no-sysbuild -b qemu_riscv32 -d "$out/e2e-qemu-riscv" "$repo/firmware/samples/connect" -- \
    -DEXTRA_CONF_FILE=overlay-qemu-riscv.conf
  python3 "$repo/firmware/tests/e2e/run_e2e.py" --qemu "$out/e2e-qemu-riscv/zephyr/zephyr.elf"

  # Real updates through the real MCUboot, on an emulated board whose flash is a file: an update interrupted by a power
  # cut, a downgrade, an image that can't reach the server and is rolled back, and an image signed with another key.
  # See firmware/tests/e2e/run_ota.py. The newest version only, because it takes six builds.
  build_ota_image base 2
  build_ota_image next 3
  # An address that can't reach the server, and a short trial so the test doesn't wait 15 minutes.
  build_ota_image bad 4 '-DCONFIG_NET_CONFIG_MY_IPV4_ADDR="192.0.2.15"' '-DCONFIG_NET_CONFIG_MY_IPV4_GW="192.0.2.1"' \
    -DCONFIG_TRACEGALE_OTA_CONFIRM_TIMEOUT_S=30
  # The same firmware as "next", signed with a key made here, which the device's MCUboot has never seen.
  imgtool="$workspace/bootloader/mcuboot/scripts/imgtool.py"
  python3 "$imgtool" keygen -k "$out/ota/other-key.pem" -t ecdsa-p256
  python3 "$imgtool" sign --version 1.5.0+0 --header-size 0x200 --slot-size 1048576 --align 4 \
    --key "$out/ota/other-key.pem" "$out/ota/next/src-next/zephyr/zephyr.bin" "$out/ota/wrong-key.bin"
  python3 "$repo/firmware/tests/e2e/run_ota.py" \
    --mcuboot "$out/ota/base/mcuboot/zephyr/zephyr.bin" \
    --base "$out/ota/base/src-base/zephyr/zephyr.signed.bin" \
    --next "$out/ota/next/src-next/zephyr/zephyr.signed.bin" \
    --bad "$out/ota/bad/src-bad/zephyr/zephyr.signed.bin" \
    --wrong-key "$out/ota/wrong-key.bin"
fi
