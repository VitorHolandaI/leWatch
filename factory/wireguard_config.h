/**
 * @file      wireguard_config.h
 * @brief     Compile-time WireGuard tunnel config for the T-Watch S3.
 *
 * Public (non-secret) tunnel parameters only. Edit before flashing, same style
 * as nextcloud_config.h / the NTP servers in factory.ino. The private key and
 * peer keys live in wireguard_secrets.h (gitignored) — see the .example.
 *
 * The tunnel lets the watch reach the ServitorAssistant server off-LAN without
 * exposing MCP (:8001) / voice (:8000) publicly. Handshake needs a synced clock,
 * so hw_vpn_begin() must run only after WiFi + SNTP. Trusted use only: the WG
 * private key lives on the device.
 */
#pragma once

// This device's address inside the WireGuard network ([Interface] Address).
// The tunnel is toggled at runtime (switch in the Wireless app, persisted in NVS
// via hw_vpn_enabled_set). Default is OFF until the user turns it on, so a
// mis-set peer can never wedge plain WiFi or crash the watch.

#ifndef WG_LOCAL_IP
#define WG_LOCAL_IP     10, 66, 66, 20
#endif

// WireGuard network subnet mask.
#ifndef WG_SUBNET
#define WG_SUBNET       255, 255, 255, 0
#endif

// Gateway inside the WireGuard network (usually the peer's tunnel IP).
#ifndef WG_GATEWAY
#define WG_GATEWAY      10, 66, 66, 1
#endif

// Local UDP port the interface binds to.
#ifndef WG_LOCAL_PORT
#define WG_LOCAL_PORT   51820
#endif

// Peer (server) public endpoint: reachable host/IP + UDP port ([Peer] Endpoint).
#ifndef WG_PEER_ENDPOINT
#define WG_PEER_ENDPOINT "195.35.42.208"
#endif
#ifndef WG_PEER_PORT
#define WG_PEER_PORT    51524
#endif

// Reachability probe target (a host:port that only answers through the tunnel,
// e.g. the ServitorAssistant server's VPN IP). Used by hw_vpn_probe() to log
// whether the tunnel actually carries traffic. Not a secret.
#ifndef WG_PROBE_HOST
#define WG_PROBE_HOST "10.66.66.16"
#endif
#ifndef WG_PROBE_PORT
#define WG_PROBE_PORT 8001
#endif

// Source IP/mask accepted from the tunnel ([Peer] AllowedIPs). Keep this narrow
// (server subnet) with make_default=false so LAN/Internet fetches still use WiFi.
#ifndef WG_ALLOWED_IP
#define WG_ALLOWED_IP   10, 66, 66, 0
#endif
#ifndef WG_ALLOWED_MASK
#define WG_ALLOWED_MASK 255, 255, 255, 0
#endif
