# Every server image scanned: no fixable vulnerability left

**Headline:** the first scan of every container image a Tracegale server runs found hundreds of known vulnerabilities, almost
all in third-party base images that hadn't been rebuilt upstream. The same day, every one that had a fix was fixed: **no
image now has a known vulnerability with a fix available, at any severity**, and the scan runs on every change, failing if
a fixable critical or high one appears.

## What was tested

A Tracegale server is six containers: the Tracegale server itself, the coredump decoder, the MQTT broker, the database, the
nginx front end, and the backup tool. Each image was scanned with Grype, which compares every package inside an image, from
the operating system's to the libraries compiled into programs, against public vulnerability databases. Syft wrote a
software bill of materials (SBOM) of each image, which Grype scanned.

## The setup

- Grype v0.121.0 and Syft v1.55.0, pinned and checked against their published checksums, run by
  `scripts/ci/image-check.sh`; the vulnerability database as of 2026-11-15.
- Commit: the one that adds this report.

## Results

**Before**, as the server was configured that morning (fixable / not fixable):

| Image | Critical | High |
|---|---|---|
| nginx (`nginx-unprivileged:1.28-alpine`, upstream) | 45 / 1 | 160 / 10 |
| PostgreSQL (`postgres:16-alpine`, upstream) | 4 / 0 | 44 / 0 |
| Backup tool (built on PostgreSQL's image) | 4 / 0 | 45 / 0 |
| MQTT broker | 1 / 0 | 0 / 3 |
| Decoder | 0 / 13 | 3 / 138 |
| Tracegale server | 0 / 0 | 1 / 0 |

**What was done:**

| Cause | Fix |
|---|---|
| nginx's 1.28 line was no longer rebuilt upstream | Moved to the current stable line, 1.30, in a thin image of Tracegale's own that takes Alpine's security fixes and removes curl, which nothing used |
| PostgreSQL's image carries `gosu`, built with an old Go | A thin image of Tracegale's own that takes Alpine's security fixes and removes `gosu`; the database now starts as its own user, so it isn't needed |
| The broker's upstream image had a zlib with a critical flaw | The broker's image takes Alpine's security fixes, and requires the fixed zlib |
| The decoder's Debian packages predated a zlib fix | Its packages come from Debian's snapshot archive at a fixed date, now moved forward, and installed packages are upgraded too |
| The server's sign-on library, go-jose v4.1.4 | Updated to v4.1.6. (Go's own scanner had found the vulnerable code unreachable; it was updated anyway) |

**After** (fixable / not fixable):

| Image | Critical | High | Medium |
|---|---|---|---|
| Tracegale server | 0 / 0 | 0 / 0 | 0 / 0 |
| nginx | 0 / 0 | 0 / 0 | 0 / 3 |
| PostgreSQL | 0 / 0 | 0 / 0 | 0 / 3 |
| Backup tool | 0 / 0 | 0 / 0 | 0 / 3 |
| MQTT broker | 0 / 0 | 0 / 3 | 0 / 4 |
| Decoder | 0 / 13 | 0 / 138 | 0 / 155 |

**The findings without a fix**, judged one by one:

- **Decoder:** the critical ones are in `libcurl`, which GDB depends on for downloading debug symbols, and in Python 3.13,
  which Zephyr's coredump tool runs on; Debian has marked them as ones it won't fix in this release. GDB's downloading is
  switched off, and the decoder has **no network at all**. It's also the most isolated part of the system: a read-only
  filesystem, no capability but changing users, GDB running as a user of its own that can write nowhere, and limits on
  memory, processes, time and output for every dump.
- **Broker:** three in cJSON, a JSON library the Mosquitto image includes for its administration plugin and tools, which
  Tracegale's broker configuration doesn't load.
- **The medium ones** in the Alpine-based images are one flaw in BusyBox, the small set of shell tools the images carry,
  with no fix yet.

They'll be fixed when fixes exist: the scan fails as soon as one does.

## What it doesn't show

- **Only known vulnerabilities**, ones already in public databases. Finding unknown ones is what the security review's
  fuzzing and other work is for.
- **The firmware isn't an image.** It's compiled into each customer's own firmware with their Zephyr; its own code is
  checked by fuzzing and review instead.
- **Scanners can miss things or report what doesn't apply**, since they match package versions; the "doesn't apply" calls
  above are judgements, explained for each.

## How to repeat it

```sh
scripts/ci/server-check.sh     # builds the images, then runs scripts/ci/image-check.sh
```

Raw results: [data/2026-11-15-image-scan/](data/2026-11-15-image-scan/), the scan tables after the fixes, and the first
run's summary.
