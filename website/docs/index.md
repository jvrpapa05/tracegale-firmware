---
hide:
  - navigation
  - toc
---

# Tracegale

<p class="hero-lead">Know what your Zephyr devices are doing in the field. Tracegale collects crash reports, metrics and logs
from your devices, turns each crash into a backtrace, and updates their firmware safely, on a server you run yourself.</p>

Turn it on in your firmware with one line:

```
CONFIG_TRACEGALE=y
```

[Get started](get-started/install.md){ .md-button .md-button--primary } [How it works](concepts/how-it-works.md){ .md-button }

<div class="grid cards" markdown>

-   **Crash reports**

    ---

    A device that crashes sends its stack and registers after it restarts. Tracegale decodes them into functions, files
    and lines, and groups crashes with the same cause into issues.

    [Read crashes and issues](get-started/crashes.md)

-   **Over-the-air updates**

    ---

    Send signed firmware a few devices at a time, through MCUboot. A bad image rolls itself back, and you can pause a
    rollout at any moment.

    [Update your devices](guides/updates.md)

-   **Metrics and logs**

    ---

    Memory, CPU, network use and your own values, for one device or the whole fleet, and the logs your devices wrote,
    searchable, even those from while they were offline.

    [Watch your fleet's metrics](guides/metrics.md)

-   **Your own server**

    ---

    One Docker Compose stack on your own machine. Each device has its own certificate, signing keys never touch the
    server, and backups are encrypted.

    [Install the server](get-started/install.md)

</div>

## Built for real fleets

| | |
|---|---|
| **Boards** | Builds for 1,642 of the 1,662 board configurations Zephyr v4.5 supports. [See the list](boards.md) |
| **Scale** | A million devices checking in every hour, on one desktop-class machine. [Read the report](reports/2026-11-15-load-1m.md) |
| **Security** | Every parser fuzzed with about 600 million hostile inputs; no known fixable vulnerability in any server image. [Test reports](reports/index.md) |
| **Versions** | Zephyr v4.5, v4.4 and v3.7 LTS, and the nRF Connect SDK v3.4, v3.3 and v2.9 |
| **Links** | Ethernet, Wi-Fi and cellular, over HTTPS or MQTT |

## Open source

The firmware module is Apache-2.0, so it can go into closed products. The server is AGPL-3.0-or-later. Copyright 2026
PHITES Embedded Research and Development on Engineering.
