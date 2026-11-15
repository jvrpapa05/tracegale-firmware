# Contributing

Thank you for helping. Tracegale is developed by PHITES Embedded Research and Development on Engineering, and this
repository receives each release.

## Reporting a problem

Open an issue with what you expected, what happened, and how to repeat it: the version, and for firmware the Zephyr version
and board. **For a security problem, don't open an issue:** follow the [security policy](SECURITY.md).

## Changes

- Open an issue first for anything larger than a small fix, so the approach can be agreed.
- Run `scripts/ci/firmware-check.sh <zephyr-tag> and scripts/ci/fuzz-firmware.sh` before sending a change.
- Commit messages say why a change was made, not only what it does.
- Never commit secrets or credentials.

## Licences

Everything here is Apache-2.0. New files need an SPDX header unless `REUSE.toml` covers them. Never copy code from other fleet platforms' SDKs.

## Developer Certificate of Origin

Every commit must carry a `Signed-off-by` line (`git commit -s`), which certifies the
[Developer Certificate of Origin](https://developercertificate.org/).
