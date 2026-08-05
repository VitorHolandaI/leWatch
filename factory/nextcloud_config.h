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

// MCP Streamable HTTP endpoint. Use the server's LAN address: the watch is not
// on the WireGuard VPN, so a 10.66.x.x VPN address will not resolve for it.
#ifndef NEXTCLOUD_MCP_URL
#define NEXTCLOUD_MCP_URL "http://192.168.0.24:8001/mcp"
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

// Default reminder lead (minutes) when an item carries no VALARM of its own.
#ifndef NEXTCLOUD_DEFAULT_LEAD_MIN
#define NEXTCLOUD_DEFAULT_LEAD_MIN 10
#endif
