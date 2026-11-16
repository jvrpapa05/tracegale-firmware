# How far one server goes

The load test: what one Tracegale server handled on one machine, how it was measured, what limits were found, and
what to add when a fleet outgrows it.

## How it was measured

- **The load:** `server/cmd/tgload`, a development tool, acts as devices. `tgload provision` creates them through the admin
  API, each with its own key and certificate, as a person adding devices would. `tgload checkin` has each device check in
  once, as firmware does: its own TLS connection with its own certificate, `HELLO`, then a `METRICS` report, then close.
  Start times are spread evenly over a window, as firmware's random wait after the network returns spreads them
  (`CONFIG_TRACEGALE_NET_WAKE_JITTER_S`, 30 seconds by default).
- **The worst case it models:** every device in the fleet coming back at once after an outage, within the firmware's
  30-second window. Day to day, a fleet of 100,000 checking in hourly sends about 28 check-ins a second, far less.
- **The server:** the standard Docker Compose stack, unchanged, from a fresh install. The load reaches nginx at its container
  address, as traffic from outside does; Docker's port proxy on the loopback address would be a bottleneck production doesn't
  have.
- **The machine:** one host with 12 CPU threads and 16 GB of RAM, running both the server and the load, so the figures are a
  floor: the load tool's own TLS work competes with the server's.

## Results

The full reports, with raw data: [100,000 devices](../reports/2026-11-15-load-100k.md) and
[1,000,000 devices](../reports/2026-11-15-load-1m.md). In short, for 1,000,000 devices: an hour of hourly check-ins was
answered in full, 99% within 57 ms, using on average 68% of one CPU thread and 450 MB for the whole server; and the whole
fleet reconnecting at once was served within 8 minutes 51 seconds.

Measured on 2026-11-15 with 100,000 devices, made through the admin API in 75 seconds (1,327 a second).

| Every device checks in once, starting over | Checked in | Told `server_busy` | Time to `HELLO`'s reply: median, p95, p99, slowest | Check-ins a second |
|---|---|---|---|---|
| 120 seconds | 100,000 | 0 | 30, 52, 55, 68 ms | 833 |
| **30 seconds, the firmware's own window: a fleet back from an outage** | **100,000** | **0** | **61, 136, 198, 454 ms** | **3,322** |
| 15 seconds, past this machine's limit | 83,752 | 16,244 | 3.0, 10.8, 12.3, 20 s | 2,353 |
| 10 seconds | 89,670 | 9,846 | 3.1, 11.8, 15.6, 24 s | 2,406 |

Each check-in is two messages, `HELLO` and `METRICS`, so 3,322 check-ins a second is 6,644 messages a second, each written
to the database before its reply.

- **The goal holds:** a fleet of 100,000 all coming back within the firmware's 30-second window checks in completely, with
  no device turned away.
- **Past the limit, it degrades and recovers.** Squeezed into 15 or 10 seconds, the machine ran out of CPU: about one device
  in ten was told `server_busy`, which firmware answers by trying again 30 seconds later, and the rest waited longer. Nothing
  failed, and the next run, two minutes later, was clean. On these runs the load tool held back its own starts too, so part
  of that limit is the tool's.
- **Memory isn't the limit:** with 100,000 devices the whole stack used about 400 MB (database 178 MB, server 112 MB,
  nginx 98 MB), and the machine still had 14.6 GB free.
- **Storage:** 100,000 devices after four rounds of check-ins took 295 MB in the database: the devices 27 MB, check-in records
  91 MB, and metric reports 138 MB.

**Found and fixed:** the first 30-second run turned 2,361 devices away, 2.4%, with a p99 of 1.15 seconds. Every check-in
waits for its database commit, and one writer committed every batch in turn, so check-ins piled up to the limit of 4,000 at
once. Four writers now commit in parallel. Each device always goes to the same writer, so its check-ins stay in order and two
writers never write the same device's row at once, which rules out deadlocks between them.

Not measured yet: MQTT under load, crash uploads and update downloads at this scale, and a long steady run.

## When a fleet outgrows one server

What helps today, in this order, measuring between steps with the server's own measurements (see "Monitor" in
`README.md`):

1. **A bigger machine, especially for the database.** It does the most work: every check-in, report and log record ends in
   a write. Watch `tracegale_db_connection_waits_total` and the database's own CPU and disk. A faster disk, more memory and
   more cores help before anything else; nginx already spreads TLS over every core.
2. **Spread check-ins in time.** A longer `CONFIG_TRACEGALE_NET_WAKE_JITTER_S` turns a fleet coming back from an outage into
   a gentler wave, at the cost of a slower first report. Devices told `server_busy` wait and try again, so an overloaded
   server catches up by itself; it never loses what devices still hold.
3. **Keep less.** Shorter retention (`TRACEGALE_LOGS_RETENTION_DAYS` and the others) keeps the database small, and
   whole days of metrics and logs go at once, cheaply.

Not supported yet, and needed to go beyond one machine:

- **The database on its own machine.** The server's connection to PostgreSQL has no TLS, which is safe only on the stack's
  own internal network, and that network has no route out. The server refuses to start with any other database host.
- **Several servers.** Each server listens on one Unix socket that nginx reaches; a second server container would replace
  the first one's socket. Over MQTT, the broker's shared subscription is already ready to split messages between servers.
