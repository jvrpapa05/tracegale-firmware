# Keeping dependencies up to date

Everything Tracegale builds from is pinned to an exact version, and checked against a hash, so a build can't silently pick up
something different, and an old commit still builds. The cost is that security fixes don't arrive by themselves. This is
how they do.

## What tells us an update is needed

| Check | Runs | Fails on |
|---|---|---|
| `govulncheck` in `scripts/ci/server-check.sh` | Every server change, locally and in CI | Any known vulnerability in Go or a Go module that the server's code reaches |
| `scripts/ci/image-check.sh` (Grype), from `server-check.sh` | Every server change | A critical or high vulnerability with a fix available, in any image the server runs |
| A monthly review | The first week of each month | Anything below: what changed upstream, even without a known vulnerability |

A check that fails because of a new vulnerability is fixed by updating, not by turning the check off. A finding that can't be
fixed yet (no fix exists, or it doesn't apply to Tracegale) is written down in the security review's report with the reason.

## What's pinned, and how to update it

| What | Where | How |
|---|---|---|
| Go, for building the server | `server/Dockerfile`, `server/decoder/Dockerfile`, `server/deploy/backup/Dockerfile` (image digest), `scripts/ci/server-check.sh` (version and SHA-256 from go.dev) | Pull the new `golang:<version>-alpine`, take its digest with `docker inspect`, and take the archive's SHA-256 from `https://go.dev/dl/?mode=json` |
| Go modules | `server/go.mod`, `server/go.sum` | `go get <module>@<version>`, `go mod tidy`; `go mod verify` runs in every check |
| Base images: distroless, Debian, PostgreSQL, nginx, Mosquitto | The Dockerfiles and `server/compose.yaml`, by digest | Pull the new tag, take the digest, update every place it appears (`grep -rn` the old digest). PostgreSQL moves only within its major version, which upgrades need no dump for |
| The decoder's Debian packages | `ARG DEBIAN_SNAPSHOT` in `server/decoder/Dockerfile` | Move the snapshot time forward; installed packages are upgraded from it too |
| Alpine packages in the database, nginx, broker and backup images | `apk upgrade` in `server/deploy/{postgres,nginx,mosquitto,backup}/Dockerfile` | Nothing to change: each build takes the fixes in Alpine's stable branch, which only takes fixes. A minimum version (`'zlib>=1.4-r0'`) is added where a needed fix must be there. This floats on purpose: there's no Alpine snapshot archive, and upstream images can lag months behind their fixes |
| Zephyr SDK | `ci/zephyr-sdk/<version>.sha256` | As `ci/zephyr-sdk/README.md` says |
| Zephyr and the nRF Connect SDK | `ci/manifests/` | A new manifest for a new release; the roadmap's supported versions say which |
| GitHub Actions | `.github/workflows/`, by commit hash with the version as a comment | Take the new release's commit hash |
| Scanners and checkers: gitleaks, Grype, Syft, govulncheck | `scripts/ci/policy-check.sh`, `scripts/ci/image-check.sh`, `scripts/ci/server-check.sh` | Versions at least two weeks old, with their published SHA-256 checked against the download |

## After any update

1. The full checks pass: every test, the scans, the end-to-end tests and the restore drill.
2. The commit says what was updated and why: which vulnerability, or the monthly review.
3. The live server is upgraded with `server/upgrade.sh`, which takes a backup first.

## Why pinned, and not floating

A floating tag (`postgres:16`, `apt-get install gdb`) gets fixes without anyone doing anything, but a rebuild can then
produce something nobody tested, and a release from a year ago can't be rebuilt as it was, which disaster recovery needs. A
compromised upstream release would also flow straight in. Pinning, plus scanning that fails when a fix is waiting, gives
both repeatable builds and timely fixes.
