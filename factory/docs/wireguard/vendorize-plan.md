# WireGuard on the T-Watch S3 — vendorize + integration plan

## Goal

Let the watch reach the ServitorAssistant server (MCP `:8001`, voice `:8000`)
over a WireGuard tunnel instead of only the trusted LAN, so the appliance works
off the home network without exposing those ports publicly.

## Library choice (settled)

- **`Tinkerforge/WireGuard-ESP32-Arduino`**, pinned at commit **`96032fd`**.
- Lineage: Donenfeld reference → `smartalock/wireguard-lwip` (Daniel Hope,
  BSD-3) → `ciniml/WireGuard-ESP32-Arduino` (stale 2022, IDF4) → **Tinkerforge**
  fork (active 2026, IDF5 fixes). No official Arduino lib exists upstream.
- Security audit result (this project, commit `96032fd`): crypto byte-identical
  to upstream `smartalock`; `x25519.c` differs only by a signature fix;
  `wireguard.c` adds secret-zeroing (positive); `wireguardif.c` is lwIP/ESP-IDF
  plumbing, no network calls outside the tunnel, no hardcoded host/key/backdoor.
  See ai-memory `notes/wireguard-esp32-audit.md`.

## What the lib actually is (walkthrough)

- **Public API** — one class, `WireGuard`, in `src/WireGuard-ESP32.h`:
  - Only the **long** `begin(localIP, Subnet, localPort, Gateway, privateKey,
    remotePeerAddress, remotePeerPublicKey, remotePeerPort, allowedIP,
    allowedMask, make_default, preshared_key, ...filters, mtu)` overload exists.
    The bundled `examples/*.ino` still call the **old 5-arg** `begin()` — those
    examples are STALE and will not compile against this fork. Ignore them.
  - `end()`, `is_initialized()`, static `derive_public_key()`.
- **Crypto** = pure-software reference C (`src/crypto/refc/*`): chacha20poly1305,
  blake2s, poly1305-donna, x25519. No mbedtls hardware crypto path.
- **RNG** (`wireguard-platform.c`) = `esp_fill_random()` (ESP32 HW RNG) seeding
  an `mbedtls_ctr_drbg`. Handshake timestamp (TAI64N) comes from `gettimeofday`.
  → **Requires system clock synced** (NTP) or the peer rejects the handshake.
  The watch already runs SNTP in `factory.ino`, so this is satisfied.
- **Deps**: `lwip/*` and `mbedtls/*` only — both ship in ESP32 Arduino core
  3.3.10. No new external dependency.

## Plan

### 1. Vendorize (pinned, audited copy)

- Copy the fork's `src/`, `library.properties`, `LICENSE` into the **sketchbook
  library** dir `~/Arduino/libraries/WireGuard-ESP32/` (standard Arduino
  location — the IDE finds it the same way it finds `LilyGoLib`). NOT inside the
  factory sketch `src/` (that folder holds LVGL image assets).
- Drop `~/Arduino/libraries/WireGuard-ESP32/VENDORED.md` recording: upstream
  URL, pinned commit `96032fd`, audit date/verdict, "do not `git pull`; re-audit
  before bumping". Delete bundled `examples/` (stale, avoid confusion).

### 2. Wrap behind the project HAL (CLAUDE.md: thin interface owns third-party)

- New HAL surface in `hal_interface.h` / `.cpp`, all under `#ifdef ARDUINO`:
  - `bool hw_vpn_begin();`   — read config/secrets, call `WireGuard::begin(...)`.
  - `void hw_vpn_end();`
  - `bool hw_vpn_up();`      — `wg.is_initialized()` + link check.
- Single static `WireGuard wg;` owned inside `hal_interface.cpp`. No LVGL/UI code
  touches the lib type directly.
- **Sim (`#else`) stubs**: `hw_vpn_begin`→false, `hw_vpn_up`→false, `hw_vpn_end`
  no-op, so `make -C sim` keeps building.

### 3. Config split — public vs secret

- `wireguard_config.h` (committed, header-guard, `#ifndef` overridable — same
  style as `nextcloud_config.h`): peer endpoint host+port, local tunnel IP,
  subnet, gateway, allowed-IP/mask, MTU.
- `wireguard_secrets.h` (**gitignored**): `WG_PRIVATE_KEY`, `WG_PEER_PUBLIC_KEY`,
  optional `WG_PRESHARED_KEY`. The WG private key MUST live on the device to
  establish the tunnel — unlike `NC_APP_PASSWORD`, it cannot stay server-side.
  Keep it out of git; add `wireguard_secrets.h` to `.gitignore`; ship a
  `wireguard_secrets.h.example` template.
- Sync: `sync_to_git.sh` must NOT copy `wireguard_secrets.h`. Verify its rsync
  excludes (or add an exclude) so the key never lands in the git mirror.

### 4. Bring-up sequence + power

- Call `hw_vpn_begin()` only AFTER WiFi is up AND SNTP has set the clock
  (handshake needs valid time). Wire into the existing post-WiFi/post-NTP path.
- `make_default=false` initially: keep LAN reachable, route only the server
  subnet through the tunnel via `allowedIP`/`allowedMask`. Avoids breaking
  weather/news LAN/Internet fetches while testing.
- Light-sleep drops the tunnel like it drops WiFi. Reuse the existing
  `set_low_power_mode_flag(false)` guard around VPN-dependent fetches (already
  done for Servitor voice + agenda sync).

### 5. Verify

- Compile-only against core **3.3.10** (the flashing target): confirm the long
  `begin()` links and lwip/mbedtls headers resolve. (Sketch won't run in sim;
  sim only needs the `#else` stubs to keep `make -C sim` green.)
- On device: `hw_vpn_up()` true after begin; ping/reach `192.168.x`/tunnel IP of
  the server; MCP agenda sync + Servitor voice work through the tunnel from off-
  LAN.

## Security notes

- Trusted use only until verified: private key on device = device compromise ⇒
  tunnel identity leak. Acceptable for a personal watch on the owner's VPN.
- Never commit `wireguard_secrets.h`. Never log the private key.
- Re-run the crypto diff audit before ever bumping past `96032fd`.

## Out of scope (future)

- On-device key provisioning UI / NVS-stored keys (vs compile-time header).
- Full-tunnel (`make_default=true`) routing once basic reach is proven.
