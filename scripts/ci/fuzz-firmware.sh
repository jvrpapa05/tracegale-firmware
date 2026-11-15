#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
# SPDX-License-Identifier: Apache-2.0
#
# Fuzzes the firmware's parsers of what a server or broker sends, on the host, with clang's libFuzzer and the address and
# undefined-behaviour sanitizers, as the security review set up. The harnesses are in firmware/tests/fuzz.
# Corpora are kept in TRACEGALE_TOOLS between runs, so each run carries on from the last. Any crash fails the check, and the
# input that caused it is kept next to the corpus.
# Needs clang with libFuzzer (Ubuntu: clang and libclang-rt-18-dev).
# Usage: scripts/ci/fuzz-firmware.sh [seconds per target, default 30]
set -euo pipefail

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
tools=${TRACEGALE_TOOLS:-$HOME/.cache/tracegale/tools}
seconds=${1:-30}
fuzz="$repo/firmware/tests/fuzz"
work="$tools/firmware-fuzz"
# shellcheck disable=SC2054 # The commas belong to the option.
flags=(-g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all -Wall -Werror
  -I"$fuzz/shim" -I"$repo/firmware/src" -I"$repo/firmware/include")

mkdir -p "$work"
# target and the firmware sources it tests.
targets=(
  "wire:$repo/firmware/src/wire.c"
  "mqtt:$repo/firmware/src/transport_mqtt.c"
  "identity:$repo/firmware/src/identity.c"
)
failed=()
for entry in "${targets[@]}"; do
  target=${entry%%:*}
  source=${entry#*:}
  echo "== Firmware fuzzing: $target, $seconds s"
  clang "${flags[@]}" "$fuzz/fuzz_$target.c" "$source" -o "$work/fuzz_$target"
  corpus="$work/corpus-$target"
  python3 "$fuzz/make_corpus.py" "$target" "$corpus"
  if ! "$work/fuzz_$target" -max_total_time="$seconds" -print_final_stats=1 -artifact_prefix="$work/crash-$target-" \
    "$corpus" >"$work/$target.log" 2>&1; then
    failed+=("$target")
    tail -n 40 "$work/$target.log" >&2
  fi
  grep -E '^Done|stat::new_units_added' "$work/$target.log"
done

if [ "${#failed[@]}" -gt 0 ]; then
  echo "Firmware fuzzing found crashes in: ${failed[*]}. Inputs are in $work/crash-*" >&2
  exit 1
fi
echo "Firmware fuzzing passed."
