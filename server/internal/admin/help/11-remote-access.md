# Reach Tracegale from outside

<!-- order: 11 -->
<!-- summary: Put Tracegale behind a reverse proxy such as Pangolin, for people and devices away from your network. -->

Devices in the field, and people away from the office, reach Tracegale through a reverse proxy. Devices and people need different
treatment, because devices prove who they are with their own certificate.

| Port | Proxy it as | Why |
|---|---|---|
| Devices, 8443 | **Raw TCP, passed straight through** | A proxy that ended TLS would hide each device's certificate, and every device would be refused |
| Admin, 8444 | Normal HTTPS | The proxy can end TLS. Tracegale's certificate comes from its own CA, so the proxy skips verifying it |

## Settings on the Tracegale server

In `server/.env`:

```
TRACEGALE_SERVER_NAMES=tracegale.example.com,192.168.1.10
TRACEGALE_ADMIN_BIND=192.168.1.10
TRACEGALE_TRUSTED_PROXY=192.168.1.20/32
```

- `TRACEGALE_SERVER_NAMES`: the public name first, because devices check it.
- `TRACEGALE_ADMIN_BIND`: the LAN address the proxy reaches.
- `TRACEGALE_TRUSTED_PROXY`: the proxy's address, so Tracegale sees each visitor's real address. Without it, everyone seems to come
  from the proxy, and shares one login limit.

Then restart: `docker compose up -d --wait && docker compose restart nginx`.

## With Pangolin

- A **raw TCP** resource for port 8443.
- An **HTTPS** resource for port 8444, with Pangolin's sign-on in front if you like. Let `/api/v1/symbols` through without
  sign-on, so build machines can upload with their token.

> With Pangolin's sign-on on, people log in twice: once at Pangolin, then with their Tracegale account.

Next: [When something goes wrong](12-troubleshooting.md).
