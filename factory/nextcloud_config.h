/**
 * @file      nextcloud_config.h
 * @brief     Compile-time config for the Nextcloud agenda sync (Agenda app).
 *
 * Edit these before flashing (Arduino IDE), the same way NTP servers are set in
 * factory.ino. No Nextcloud credentials live here or on the device: the watch
 * talks only to the ServitorAssistant MCP server, which is the CalDAV gateway.
 * See docs/mcp/nextcloud-appliance-sync.md for the full contract.
 */
#pragma once

// MCP Streamable HTTP endpoint. Primary + fallback: the appliance tries the
// primary, then the fallback if it fails. LAN first (192.168.0.24) so home use
// works even with the VPN off; the VPN address (10.66.66.16, inside
// WG_ALLOWED_IP) is the away fallback. Swap the two to prefer the tunnel.
#ifndef NEXTCLOUD_MCP_URL
#define NEXTCLOUD_MCP_URL "http://192.168.0.24:8001/mcp"
#endif
#ifndef NEXTCLOUD_MCP_URL_FALLBACK
#define NEXTCLOUD_MCP_URL_FALLBACK "http://10.66.66.16:8001/mcp"
#endif

// Local timezone offset from UTC, in minutes (Campina Grande / America/Recife
// is UTC-3 and observes no DST). Used to convert the snapshot's UTC instants to
// the watch's local RTC time for display and reminder timing.
#ifndef NEXTCLOUD_TZ_OFFSET_MIN
#define NEXTCLOUD_TZ_OFFSET_MIN (-180)
#endif

// How many days of agenda to request (bounded 1..31 by the server).
#ifndef NEXTCLOUD_SYNC_DAYS
#define NEXTCLOUD_SYNC_DAYS 7
#endif

// ServitorAssistant voice endpoint (FastAPI on port 8000). Primary + fallback,
// same LAN-first / VPN-fallback logic as the MCP URL above. The watch uploads a
// recorded WAV and gets back the LLM answer as text or synthesized audio.
#ifndef SERVITOR_API_URL
#define SERVITOR_API_URL "http://192.168.0.24:8000/file_recorded"
#endif
#ifndef SERVITOR_API_URL_FALLBACK
#define SERVITOR_API_URL_FALLBACK "http://10.66.66.16:8000/file_recorded"
#endif

// Default reminder lead (minutes) when an item carries no VALARM of its own.
#ifndef NEXTCLOUD_DEFAULT_LEAD_MIN
#define NEXTCLOUD_DEFAULT_LEAD_MIN 10
#endif
