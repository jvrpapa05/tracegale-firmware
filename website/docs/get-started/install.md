# Install the server

Tracegale's server runs on one Linux machine with Docker. It takes about ten minutes.

## What you need

- A Linux machine, or a virtual machine, with [Docker and Docker Compose](https://docs.docker.com/engine/install/).
  2 CPU cores, 2 GB of memory and 20 GB of disk leave plenty of room: in testing, the whole server used about 450 MB of
  memory and two-thirds of one CPU thread for a million devices checking in hourly.
- A name or IP address that your devices can reach the machine at, such as `fleet.example.com` or `192.168.1.20`.

## Start it

1. Get Tracegale's server and go into its `server` folder.

    !!! note "Early access"
        The server will be published as open source once it's proven on real hardware. Until then, developers and
        hobbyists can have early access: open an issue titled "Early access" in the
        [firmware repository](https://github.com/jvrpapa05/tracegale-firmware/issues).

2. Start it, with the name or address your devices will use:

    ```
    TRACEGALE_SERVER_NAMES=fleet.example.com docker compose up -d --wait
    ```

    The first start builds the server and takes a few minutes. Then every part shows as **Healthy**.

3. Get the one-time setup code:

    ```
    docker compose exec server /tracegale setup-code
    ```

4. On the same machine, open `https://localhost:8444` in a browser. It warns about the certificate, because the server
   makes its own; continue to the site. Enter the code, then choose a username and a password, and choose **Create account**.

You're in. Next: [Getting started](getting-started.md) shows your way around.

!!! tip "Opening the web view from another computer"
    The web view answers only on the server itself until you choose otherwise. To open it from your network, set
    `TRACEGALE_ADMIN_BIND` to the server's address and restart with `docker compose up -d`. To reach it from outside
    your network, see [Reach Tracegale from outside](../guides/remote-access.md).

## The ports

| Port | Used by | Open to |
|---|---|---|
| 8443 | Devices, over HTTPS | The network |
| 8883 | Devices, over MQTT | The network |
| 8444 | The web view and the API | This machine only, until you set `TRACEGALE_ADMIN_BIND` |

## Keep it safe

Before you add real devices:

- **Back it up.** The server holds the keys every device trusts. [Back up](../reference/server/back-up.md) shows how to
  make encrypted backups.
- **Keep it up to date.** [Upgrade](../reference/server/upgrade.md) moves to a new version, backing up first.
