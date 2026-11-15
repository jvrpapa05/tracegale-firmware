<!--
SPDX-FileCopyrightText: 2026 PHITES Embedded Research and Development on Engineering
SPDX-License-Identifier: Apache-2.0
-->

# Security policy

Tracegale runs on devices in the field and on servers that hold their crash data, so security reports are welcome and are
handled before anything else.

## Reporting a problem

**Don't open a public issue, pull request or discussion for a security problem.**

Use GitHub's private reporting: on this repository, open the **Security** tab and choose **Report a vulnerability**. Only the
maintainers can read the report.

Please include:

- What's affected: the firmware module, the server, or a tool, and the commit or version.
- What an attacker can do, and what they need first, such as a device's credentials or an administrator's account.
- Steps to reproduce it, or a proof of concept.
- The Zephyr version and board, for firmware problems.

## What happens next

| When | What |
|---|---|
| Within 7 days | We confirm we have the report |
| Within 30 days | We tell you whether we accept it, and how we plan to fix it |
| When a fix is ready | We publish it with a security advisory, and credit you unless you'd rather not be named |

Please give us 90 days before publishing details, or until the fix is released if that's sooner. If a problem is already being
used against real devices, tell us, and we'll move faster and warn users first.

Tracegale is maintained by a small team with no bug bounty.

## Supported versions

Tracegale hasn't reached version 1. Until it does, only the newest commit on `main` gets security fixes. This policy will list
supported releases from v1.

## Scope

In scope: everything in this repository, which is the Zephyr module (`firmware/`, `zephyr/`) and its tools (`tools/`,
`scripts/`). The server is in [tracegale-server](https://github.com/jvrpapa05/tracegale-server), under the same policy.

Out of scope:

- Servers you don't own. Test against your own installation only.
- Problems in Zephyr, Mbed TLS, MCUboot, nginx, PostgreSQL or GDB themselves. Report those to their projects. Do tell us if
  Tracegale's use of them makes a problem worse.
- Weaknesses that are already listed, unless you've found a way to use one that the documents don't describe.

## How Tracegale checks itself

Every release has passed these, in the project's own checks (`scripts/ci/firmware-check.sh` and `scripts/ci/fuzz-firmware.sh` here) and its CI:

- **Known vulnerabilities:** `govulncheck` on the server's Go code and modules, and Grype on every image the server runs,
  failing on anything the code reaches or any critical or high finding with a fix.
- **Software bills of materials:** an SPDX SBOM for each image and for the source, written by Syft.
- **Fuzzing:** the server's parsers of what devices send, and the firmware's parsers of what a server or broker sends, the
  firmware's with memory and undefined-behaviour sanitizers.
- **Secrets and licences:** gitleaks on the whole history, and REUSE on every file.
- **End to end:** the whole server stack with real firmware, including attacks on the device port, the decoder's sandbox,
  and a restore from backup onto a new install.

Everything Tracegale builds from is pinned to an exact version and checked against a hash; how updates are taken is in
[docs/security/dependency-updates.md](docs/security/dependency-updates.md). Results of the security review are reported in
[tracegale-server's docs/reports/](https://github.com/jvrpapa05/tracegale-server/tree/main/docs/reports).

Images aren't published yet: each installation builds its own from this source. Published images will be signed.

## What's already known

- The threat model, with the status of each threat: [docs/security/threat-model.md](docs/security/threat-model.md).
- What isn't done yet: the "Known limits" in the release notes, [CHANGELOG.md](CHANGELOG.md).
