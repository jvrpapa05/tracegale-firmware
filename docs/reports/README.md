# Test reports

Each significant test of Tracegale has a report here, written so that anyone, engineer or not, can understand what was
tested and what the result means, and so that the results can be quoted.

Every report has the same parts:

- **The headline:** the result in plain words.
- **What was tested, and how.**
- **The setup:** the machine, the software versions, and the exact commit.
- **The results**, in full, with the raw data in [`data/`](data/).
- **What it doesn't show.** Quote a result with its limits; don't claim more than was measured.
- **How to repeat it**, so anyone can check.

| Date | Report | Headline |
|---|---|---|
| 2026-11-16 | [Tracegale on every board Zephyr supports](2026-11-15-board-survey.md) | 1,642 of 1,662 board configurations build the one line, 178 build the whole stack; the survey found and fixed a random-number security gap |
| 2026-11-15 | [Two MQTT clients, measured](2026-11-15-mqtt-clients.md) | Tracegale's own 2.2 KB MQTT client, or Zephyr's library shared with the application; both pass the same end-to-end checks; devices now work behind name-routing proxies |
| 2026-11-15 | [1,000,000 devices on one server](2026-11-15-load-1m.md) | A million devices checking in hourly, 99% answered within 57 ms, on under one CPU thread and 450 MB; all of them back after an outage within 9 minutes |
| 2026-11-15 | [100,000 devices back online in 30 seconds](2026-11-15-load-100k.md) | All 100,000 checked in, none turned away, 99% answered within 230 ms, on a 6-core desktop processor |
| 2026-11-15 | [AI agents can use Tracegale](2026-11-15-agent-integration.md) | An MCP server lets agents set Tracegale up and read the fleet, read-only by default, never seeing a device's private key |
| 2026-11-15 | [600 million hostile inputs, no crash](2026-11-15-fuzzing.md) | Every parser of untrusted data, on the server and in the firmware, fuzzed: about 600 million inputs, no crash or memory error |
| 2026-11-15 | [Every server image scanned: no fixable vulnerability left](2026-11-15-image-scan.md) | Hundreds of known vulnerabilities in third-party base images fixed the same day; none with a fix remains in any image |
| 2026-11-15 | [Known vulnerabilities: found, fixed and deployed the same day](2026-11-15-vulnerability-scan.md) | One reachable vulnerability (a server crash in Go's HTTP server) and three unreachable ones, all fixed; scanned on every change since |
| 2026-11-15 | [Lose the server, restore it, and devices carry on](2026-11-15-restore-drill.md) | An encrypted backup restored onto a new install brings back everything, and devices reconnect with their own credentials |

## The test machine

Unless a report says otherwise, tests ran on one machine:

| | |
|---|---|
| CPU | AMD Ryzen 5 8500G: 6 cores, 12 threads, a desktop processor |
| Memory | 16 GB |
| Disk | 128 GB |
| System | Ubuntu 24.04 in an LXC container on Proxmox, Linux 6.17 |
| Docker | 29.1 |

The server and the load-generating tool ran on this same machine, so the tool's own work competed with the server's. A
server on a machine of its own does at least as well.
