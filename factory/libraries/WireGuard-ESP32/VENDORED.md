# Vendored copy — do not edit in place, do not `git pull`

## LOCAL PATCH (2026-08-10) — do NOT lose on re-vendor
Arduino ESP32 core builds lwIP with `LWIP_NUM_NETIF_CLIENT_DATA = 0`, so
`netif_alloc_client_data_id()` asserts and reboots ("[wg-crash] stage=2").
Since this firmware runs exactly ONE WireGuard netif, the device pointer is kept
in a single static instead of netif client-data:
- `src/wireguardif.c`: added `static wireguard_device *wg_single_device` +
  `#undef`/`#define` of `netif_get/set_client_data` to use it.
- `src/WireGuard.cpp`: removed the `netif_alloc_client_data_id()` call.
Revert both if `LWIP_NUM_NETIF_CLIENT_DATA` is ever >= 1 in the core.


- **Upstream**: https://github.com/Tinkerforge/WireGuard-ESP32-Arduino
- **Pinned commit**: `96032fd0855d355b6ae858b41e2e8b9b4e76c916`
  ("Revert \"wireguardif.c: call update_peer_info_fn when sending a keepalive.\"")
- **Vendored**: 2026-08-09
- **Target**: ESP32 Arduino core 3.3.10 (IDF5)

## Why this fork

No official Arduino WireGuard library exists. Lineage: Jason Donenfeld's
reference → `smartalock/wireguard-lwip` (Daniel Hope, BSD-3) →
`ciniml/WireGuard-ESP32-Arduino` (stale 2022, IDF4) → this Tinkerforge fork
(active, IDF5 crash fixes).

## Security audit (commit 96032fd)

Crypto (`src/crypto/refc/*`) diffed against upstream `smartalock/wireguard-lwip`:
- chacha20poly1305 / blake2s / chacha20 / poly1305: **0 lines changed**.
- `x25519.c`: only a function-signature fix.
- `wireguard.c`: adds secret-zeroing on handshake destroy (positive).
- `wireguardif.c`: lwIP/ESP-IDF plumbing only; no network calls outside the
  tunnel, no hardcoded host/key, no backdoor.

Verdict: **clean**. Full notes in ai-memory `notes/wireguard-esp32-audit.md`.

## Rules

- Only the long `WireGuard::begin(...)` overload exists in this fork. The
  upstream `examples/*.ino` (removed here) call a stale 5-arg `begin()` and will
  not compile.
- Re-run the crypto diff audit before bumping past `96032fd`.
- Contents belong to the T-Watch factory project; wrapped behind
  `hw_vpn_*` in `hal_interface.cpp` (CLAUDE.md: thin interface owns third-party).
