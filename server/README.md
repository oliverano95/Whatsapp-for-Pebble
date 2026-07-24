# Pebble WhatsApp Bridge - Server

This is the backend that connects to WhatsApp Web (via `whatsapp-web.js`) and
exposes a simple HTTP API that the Pebble app talks to.

It's fully containerized and works the same way whether you're running it on
an x86_64 machine (a spare laptop, a NUC, a cloud VM) or an ARM64 board
(Raspberry Pi). No code changes needed between architectures - Docker and
`apt` handle that automatically.

## Quick start

1. **Copy the environment template and set your own API key:**

   ```bash
   cp .env.example .env
   ```

   Edit `.env` and set `API_KEY` to a long, random string. This is the
   password your Pebble app (and anything else calling the API) must send in
   the `x-api-key` header.

2. **Build and start the container:**

   ```bash
   docker compose up -d --build
   ```

3. **Scan the QR code:**

   The very first time it starts, it has no WhatsApp session yet. Watch the
   logs:

   ```bash
   docker compose logs -f whatsapp-bridge
   ```

   A QR code will print directly in the terminal. Scan it from WhatsApp on
   your phone: **Settings → Linked Devices → Link a Device**.

4. **Done.** Once linked, the session is saved into the `wwebjs-auth` Docker
   volume, so restarting the container later won't require re-scanning.

## Migrating an existing session (e.g. from a Raspberry Pi)

If you already have a working `.wwebjs_auth` folder from a previous
deployment and don't want to re-scan the QR code:

```bash
docker cp /path/to/existing/.wwebjs_auth/. whatsapp-bridge:/app/.wwebjs_auth
docker restart whatsapp-bridge
```

Note: WhatsApp sometimes invalidates a session if it detects a very different
environment (e.g. a different Chromium build) - if it doesn't carry over
cleanly, just scan the QR code fresh. Nothing else is lost.

## Environment variables

| Variable                     | Required | Default             | Description                                                        |
| ----------------------------- | -------- | -------------------- | -------------------------------------------------------------------- |
| `API_KEY`                     | Yes      | -                    | Password required in the `x-api-key` header for all routes except `/clay` and `/health` |
| `PORT`                        | No       | `3001`               | Port the HTTP server listens on                                     |
| `PUPPETEER_EXECUTABLE_PATH`   | No       | `/usr/bin/chromium`  | Path to the Chromium binary (already set correctly in the Dockerfile) |
| `SESSION_PATH`                | No       | `.wwebjs_auth`        | Where the WhatsApp session is stored (already set correctly in the Dockerfile) |

## Exposing it to the internet

This container only exposes port `3001` on your local network by default.
To reach it from your Pebble app over the internet, put a reverse proxy
(e.g. Nginx Proxy Manager, Caddy, Traefik) and a tunnel (e.g. Cloudflare
Tunnel) in front of it, pointing at `<host-ip>:3001`.

Two endpoints are intentionally public (no API key required), since the
Pebble app and monitoring tools need to reach them without authentication:

- `/clay` - the Pebble configuration page
- `/health` - a health check endpoint (e.g. for Uptime Kuma)

Everything else requires the `x-api-key` header to match your `API_KEY`.

## Reinstalling / recovering from a broken setup

If you ever mess up your Docker container, this whole server can be
redeployed from scratch in a couple of minutes:

```bash
git clone https://github.com/oliverano95/Whatsapp-for-Pebble.git
cd Whatsapp-for-Pebble/server
cp .env.example .env
# edit .env with your API_KEY
docker compose up -d --build
```

If you have a backup of the `.wwebjs_auth` folder, copy it in afterward (see
"Migrating an existing session" above) to skip re-scanning the QR code.
