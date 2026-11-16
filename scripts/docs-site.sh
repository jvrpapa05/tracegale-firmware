#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
# SPDX-License-Identifier: Apache-2.0
#
# Builds the docs site into out/site with Zensical: scripts/docs_site.py assembles its pages from website/, the in-app Help
# guides, the firmware, server and protocol guides, the board list and the test reports; Zensical turns them into a static
# site. Zensical and its dependencies are installed from website/requirements.txt, pinned by hash.
# Usage: scripts/docs-site.sh [--serve]   --serve also serves it at http://127.0.0.1:8000 while you edit
set -euo pipefail

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
tools=${TRACEGALE_TOOLS:-$HOME/.cache/tracegale/tools}
venv="$tools/docs-venv"
stamp=$(sha256sum "$repo/website/requirements.txt" | cut -d' ' -f1)
if [ "$(cat "$venv/.stamp" 2>/dev/null)" != "$stamp" ]; then
  rm -rf "$venv"
  python3 -m venv "$venv"
  "$venv/bin/pip" install --quiet --require-hashes --no-deps -r "$repo/website/requirements.txt"
  echo "$stamp" >"$venv/.stamp"
fi

work="$repo/out/site-src"
python3 "$repo/scripts/docs_site.py" "$repo" "$work"
cd "$work"
if [ "${1:-}" = "--serve" ]; then
  exec "$venv/bin/zensical" serve
fi
# Strict: a broken link or a page missing from the navigation fails the build.
"$venv/bin/zensical" build --clean --strict
rm -rf "$repo/out/site"
mv "$work/site" "$repo/out/site"
echo "The docs site is in $repo/out/site: $(find "$repo/out/site" -name '*.html' | wc -l) pages."
