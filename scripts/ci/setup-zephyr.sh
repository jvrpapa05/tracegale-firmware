#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
# SPDX-License-Identifier: Apache-2.0
#
# Creates a Zephyr workspace around this repository. CI and developers run the same script.
# The repository must be in a folder of its own inside the workspace, named "tracegale".
# Linux x86_64 only.
# Usage: scripts/ci/setup-zephyr.sh <zephyr-tag>, for example v4.5.0
set -euo pipefail

toolchains=(arm-zephyr-eabi riscv64-zephyr-elf)

# SDK 1.x names and places its toolchains differently from SDK 0.x.
toolchain_archive() {
  case "$1" in
    0.*) echo "toolchain_linux-x86_64_$2.tar.xz" ;;
    *) echo "toolchain_gnu_linux-x86_64_$2.tar.xz" ;;
  esac
}

toolchain_folder() {
  case "$1" in
    0.*) echo "$HOME/zephyr-sdk-$1" ;;
    *) echo "$HOME/zephyr-sdk-$1/gnu" ;;
  esac
}

sdk_installed() {
  local version toolchain
  version=$1
  [ -x "$HOME/zephyr-sdk-$version/setup.sh" ] || return 1
  for toolchain in "${toolchains[@]}"; do
    [ -d "$(toolchain_folder "$version")/$toolchain" ] || return 1
  done
}

# github.com release downloads can fail for long periods while the API keeps working, so this tries the API
# first. "gh" needs a login, or GH_TOKEN in CI.
download_release_files() {
  local version=$1 target=$2 name
  shift 2
  if command -v gh >/dev/null && gh auth status >/dev/null 2>&1; then
    local patterns=()
    for name in "$@"; do
      patterns+=(--pattern "$name")
    done
    if gh release download "v$version" --repo zephyrproject-rtos/sdk-ng --dir "$target" --clobber "${patterns[@]}"; then
      return 0
    fi
    echo "Download through the GitHub API failed. Trying github.com." >&2
  fi
  for name in "$@"; do
    curl --fail --location --silent --show-error --retry 5 --retry-delay 15 --retry-all-errors \
      --output "$target/$name" \
      "https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v$version/$name"
  done
}

# Installs only the toolchains needed here. Neither the SDK's setup.sh nor its older releases verify what they
# download, so this script verifies every file itself, against checksums kept in this repository (ci/zephyr-sdk/): the
# release's own sha256.sum would come from the same place as the files it vouches for.
install_sdk() {
  local version=$1 sdk download name toolchain pinned
  local archives=("zephyr-sdk-${version}_linux-x86_64_minimal.tar.xz" hosttools_linux-x86_64.tar.xz)

  pinned="$repo/ci/zephyr-sdk/$version.sha256"
  if [ ! -f "$pinned" ]; then
    echo "Zephyr SDK $version has no pinned checksums. Add them as ci/zephyr-sdk/README.md says." >&2
    exit 1
  fi
  sdk="$HOME/zephyr-sdk-$version"
  download=$(mktemp -d)
  for toolchain in "${toolchains[@]}"; do
    archives+=("$(toolchain_archive "$version" "$toolchain")")
  done

  download_release_files "$version" "$download" "${archives[@]}"
  for name in "${archives[@]}"; do
    grep -q -F "  $name" "$pinned" || {
      echo "No pinned checksum for $name in $pinned" >&2
      exit 1
    }
    (cd "$download" && grep -F "  $name" "$pinned" | sha256sum --check --strict)
  done

  tar --extract --xz --file "$download/${archives[0]}" --directory "$HOME"
  mkdir -p "$(toolchain_folder "$version")"
  for toolchain in "${toolchains[@]}"; do
    tar --extract --xz --file "$download/$(toolchain_archive "$version" "$toolchain")" \
      --directory "$(toolchain_folder "$version")"
  done
  cp "$download/hosttools_linux-x86_64.tar.xz" "$sdk/"
  (cd "$sdk" && ./setup.sh -h -c)
  rm -rf "$download"
}

if [ "$#" -ne 1 ]; then
  echo "Usage: $0 <zephyr-tag>" >&2
  exit 2
fi
tag=$1

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
workspace=$(dirname "$repo")
manifest="ci/manifests/west-$tag.yml"

if [ "$(basename "$repo")" != "tracegale" ]; then
  echo "The repository folder must be named 'tracegale' (found '$(basename "$repo")')." >&2
  exit 2
fi
if [ ! -f "$repo/$manifest" ]; then
  echo "No manifest for Zephyr $tag: $manifest" >&2
  exit 2
fi

cd "$workspace"

python3 -m venv .venv
# shellcheck disable=SC1091
source .venv/bin/activate
pip install --quiet --upgrade pip
pip install --quiet "west==1.5.0"

[ -d .west ] || west init -l --mf "$manifest" tracegale
west update --narrow -o=--depth=1
# Only what builds and tests need: the full requirements.txt adds compliance tools with heavy build needs.
pip install --quiet \
  -r zephyr/scripts/requirements-base.txt \
  -r zephyr/scripts/requirements-build-test.txt \
  -r zephyr/scripts/requirements-run-test.txt \
  -r bootloader/mcuboot/scripts/requirements.txt
# Espressif boards need the vendor's image tool to build. Only manifests with that module install it.
if [ -f modules/hal/espressif/zephyr/requirements.txt ]; then
  pip install --quiet -r modules/hal/espressif/zephyr/requirements.txt
fi
# The nRF Connect SDK's build runs tools of its own, such as its devicetree and partition scripts.
if [ -f nrf/scripts/requirements-build.txt ]; then
  pip install --quiet -r nrf/scripts/requirements-build.txt
fi
west zephyr-export

sdk_version=$(cat zephyr/SDK_VERSION)
if sdk_installed "$sdk_version"; then
  echo "Zephyr SDK $sdk_version is already installed."
  # A cached SDK still has to be registered with CMake on this machine.
  (cd "$HOME/zephyr-sdk-$sdk_version" && ./setup.sh -c)
else
  install_sdk "$sdk_version"
fi
