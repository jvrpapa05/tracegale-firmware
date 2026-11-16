# Lose the server, restore it, and devices carry on

**Headline:** a Tracegale server was backed up while running, destroyed completely, and restored onto a new install from
its encrypted backup. Everything came back, including the keys that devices trust, and a real device reconnected with the
credentials it already had, with nothing re-provisioned. This drill runs automatically on every full check of the code.

## What was tested

Losing a server's keys is the worst thing that can happen to a device fleet: every device would have to be visited and given
new credentials. So the backup has to hold those keys, and has to be safe to store.

The drill, `server/test/restore_drill.py`:

1. Starts a server and fills it: an administrator account with an API token, a device running Tracegale's real firmware
   (built for Zephyr's simulator) that checks in, a symbol file for decoding crashes, a signing key and a firmware release.
2. Makes an encryption key pair and backs the server up with `backup.sh` while it runs.
3. Destroys the server and all its data, as a lost machine would be.
4. Restores the backup onto a brand-new install with `restore.sh`.
5. Checks what came back, and that the same device, still running, checks in to the restored server.
6. Tries the restores that must be refused.

## The setup

- The test machine described in [README.md](README.md), with Docker.
- Tracegale as it ships, the standard Docker Compose stack. Commit `e769105` (backups added in `d4eaed3`).
- The firmware: Tracegale's `connect` sample built for Zephyr's `native_sim`.

## Results

All 6 checks passed:

| Check | Result |
|---|---|
| A backup is made while the server runs, encrypted, with only a plain list of contents readable; no key or device name appears in it | Passed |
| A backup is refused if no encryption key is set, so nothing is ever written unencrypted | Passed |
| The restore brings back the certificate authorities, the account and its API token, devices, symbol files and releases | Passed |
| The same device, with the credentials it already had, checks in to the restored server | Passed |
| A restore onto a server that has already run is refused, so a working server's keys can't be replaced by mistake | Passed |
| A backup with the wrong key, or from a newer version of Tracegale, is refused before anything is written | Passed |

**How backups are protected:** each backup is encrypted with [age](https://age-encryption.org), an open standard, to a public
key. The server holds only that public key, so someone who breaks into the server can't read its old backups; the key that
decrypts is kept by the operator, away from the server. Because the format is standard, a backup can be opened with the
ordinary `age` tool even if Tracegale's own software were lost.

## What it doesn't show

- **Same machine, new install.** The drill restores onto a fresh install on the same machine; a restore onto a different
  machine is on the maintainers' own test list.
- **A small server.** The drill's server holds one device; how long a backup and restore of a large fleet takes wasn't
  measured.
- **Up to a day lost.** A daily backup can lose up to a day of records; devices resend what they still hold, but not what
  they had already delivered.
- **Running backups is the operator's job:** scheduling them, copying them off the machine, and keeping the key safe.
  `docs/server/README.md` says how.

## How to repeat it

From a checkout, with Docker and a Zephyr build of the `connect` sample for `native_sim`:

```sh
python3 server/test/restore_drill.py <path to build/zephyr/zephyr.exe>
```

The project's full checks build the firmware and run the drill on every change. Raw output:
[data/2026-11-15-restore-drill/drill-output.txt](data/2026-11-15-restore-drill/drill-output.txt).
