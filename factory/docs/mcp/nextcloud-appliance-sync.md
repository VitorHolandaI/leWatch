# Nextcloud 7-day sync for the T-Watch appliance (via MCP)

## 0. Goal and window

The appliance (T-Watch S3 firmware in this repo) shows the user's Nextcloud
calendar events and tasks for the next seven days and notifies before each one,
the way Nextcloud itself does.

The appliance does **not** talk CalDAV directly and does **not** depend on the
LLM agent's text answers. It calls the ServitorAssistant MCP server
(repo `/home/vitor/tinker_git/ServitorAssisstant`) directly (JSON-RPC
`tools/call` over Streamable HTTP) and consumes a **structured JSON snapshot** of
a half-open range:

```text
[ local day 00:00:00 , local day + 7 days 00:00:00 )
```

Example — clock synced any time on `2026-08-04` in `America/Recife`:

```text
2026-08-04 00:00:00 -03  <=  item  <  2026-08-11 00:00:00 -03
```

Seven local calendar days including the sync day; the end instant is exclusive.
The local date is derived from the **timezone the server reports/configures**,
not from UTC alone.

This document has two parts: **Part 1** is an honest inventory of what exists
today; **Part 2** is the recommended contract to build against. Parts 3–5 cover
the appliance side, security, and the split implementation checklist.

---

# PART 1 — What exists today

## 1.1 The MCP server

- Project: ServitorAssistant, Python 3.11, **FastMCP** (official `mcp` SDK),
  **Streamable HTTP**, **stateless**, **no auth**.
  `api/mcp_module/stremable_http/stream2.py:24,797`.
- Endpoint: `http://SERVER_IP:8001/mcp`.
  - Appliance-facing (LAN) address: **`http://192.168.0.24:8001/mcp`** — the watch
    is not on the WireGuard VPN, so it must use the server's LAN IP.
  - `10.66.66.16` is the same server over WireGuard; reachable from VPN hosts only,
    **not** from the watch. The curl below was run over the VPN address; the
    response is identical on the LAN address.

### Transport is proven live

Real `initialize` + `tools/call` against the deployed server returns real data,
not just a handshake. Reproduction (protocol `2025-03-26`):

```bash
curl -sS -m 15 -X POST http://10.66.66.16:8001/mcp \
  -H 'Content-Type: application/json' \
  -H 'Accept: application/json, text/event-stream' \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"probe","version":"1.0"}}}'

curl -sS -m 15 -X POST http://10.66.66.16:8001/mcp \
  -H 'Content-Type: application/json' \
  -H 'Accept: application/json, text/event-stream' \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"list_nextcloud_tasks","arguments":{}}}'
```

Response is **SSE-framed**, with the tool result wrapped in MCP `content` blocks:

```text
event: message
data: {"jsonrpc":"2.0","id":2,"result":{"content":[{"type":"text","text":"Showing 10 of 44 Nextcloud tasks:\n[c34490b7] Post Cluster ARM vs X86 ... | pending | due 2026-05-10 16:30 -03 | list LinkedInPost\n..."}]}}
```

So: connectivity, JSON-RPC, and CalDAV read-through all work today (44 tasks
found, 10 returned). **The gap is not connectivity — it is the shape of the
output** (a formatted string) and the lack of a multi-day snapshot.

## 1.2 Current Nextcloud tools (all return a formatted string)

Registered in `stream2.py:291-524`. **Every tool returns `str`**, formatted for
an LLM, bounded to ~20 items / 4096 chars.

| Tool | Purpose | Output |
|------|---------|--------|
| `list_nextcloud_events(date, limit)` | Events for **one** local day; recurring events expanded | `str` |
| `list_nextcloud_tasks(show_completed, limit, calendar)` | Bounded task list | `str` |
| `get_nextcloud_task(task, calendar)` | One task, full detail | `str` |
| `create_nextcloud_task(...)` | Create task + reminders | `str` |
| `set_nextcloud_task_reminder(...)` | Add/update task reminder | `str` |
| `update_nextcloud_task(...)` | Update fields/status | `str` |
| `complete_nextcloud_task(...)` | Mark complete | `str` |
| `move_nextcloud_task(...)` | Move between lists | `str` |
| `delete_nextcloud_task(...)` | Delete | `str` |

Verbatim output shapes (do **not** parse these in the appliance):

```text
Calendar for 2026-08-04 (current time 09:30:00 -03): showing 2 of 2 events.
[ongoing] 09:00-10:00 | Meeting | Personal | Room A
[upcoming] 14:00 | Other | Work
```

```text
Showing 3 of 10 Nextcloud tasks:
[ab12cd34] Review document | pending | due 2026-08-06 17:00 -03 | list TrabalhoFNDE
```

Task UID is **truncated to 8 chars** in the text; times are `HH:MM` local
strings with no ISO/UTC form. These formats are optimized for LLM context and
truncation, not for machine sync.

## 1.3 Data layer already capable

The CalDAV layer under the tools already has what a JSON snapshot needs — only
no tool exposes it:

- Dynamic calendar discovery, no hard-coding: `discover_calendars` (`nextcloud_tasks.py:122`).
  Existing VTODO lists include `Tasks, LinkedInPost, Aprender, ProjetosList,
  NPU_PROJECTS, CursoFoco, TrabalhoFNDE`. Names can change — never compile them in.
- Events parsed to dicts with `UID, SUMMARY, DESCRIPTION, LOCATION, DTSTART,
  DTEND, STATUS, RECURRENCE-ID` + derived `calendar, href, start_datetime,
  end_datetime, all_day`; `CANCELLED` skipped. `_calendar_event_query` (`nextcloud_tasks.py:362`)
  **already accepts an arbitrary `range_start`/`range_end`** with server-side
  `<c:expand>` recurrence expansion — a 7-day call is one step away.
- Tasks (VTODO) parsed with `UID, SUMMARY, DESCRIPTION, DTSTART, DUE, STATUS,
  COMPLETED, PERCENT-COMPLETE, CREATED, LAST-MODIFIED, CATEGORIES, PRIORITY`
  + derived `calendar, href, etag, due_datetime`. Status values in use:
  `NEEDS-ACTION, IN-PROCESS, COMPLETED, CANCELLED`.
- All instants normalized to **UTC** internally.
- Reminders are represented **twice** by design: a `VALARM` inside the VTODO,
  **and** a linked transparent VEVENT carrying `X-SERVITOR-TASK-UID`
  (`nextcloud_tasks.py:1394-1467`). A consumer must hide/label that event to
  avoid a duplicate user-facing item.
- Credentials live only on the server (`NC_URL/NC_USER/NC_APP_PASSWORD`, HTTPS +
  Basic auth). The appliance never receives `NC_APP_PASSWORD`.

## 1.4 Appliance building blocks already in this firmware

| Need | Exists | Location |
|------|--------|----------|
| WiFi up? | `hw_get_wifi_connected()` | `hal_interface.cpp:1828` |
| Current time | `hw_get_date_time(struct tm&)` (no epoch getter → use `mktime`) | `hal_interface.cpp:1453` |
| Flash storage | `Preferences`/NVS (dedicated namespaces) + FFat (`FILESYSTEM` macro) | `hal_interface.cpp:88` |
| Background fetch | weather task: `xTaskCreate(...,24576,...)` + `s_ready/s_fetching` flags + LVGL `poll_cb` | `ui_weather.cpp:204-269` |
| Foreground alert | `ui_alarm_ring()` full-screen overlay + sound + vibrate; `hw_alarm_ringing()` flag | `ui_clock.cpp:350` |
| App + launcher | `create_app_cell(...)`; module pattern in `ui_share.cpp` (`app_t` setup/exit, `create_menu`, back handler) | `ui_main.cpp:153` |

## 1.5 Gaps (what is missing on each side)

**Server (MCP):** no JSON-returning tool; no multi-day/snapshot tool; no
`complete`/tombstone semantics; no MCP auth/TLS.

**Appliance (firmware):** `hw_http_get` (`hal_interface.cpp:3598`) is **GET only,
no auth, no custom headers, fixed truncating buffer** — insufficient for MCP,
which needs an HTTP **POST** with `Accept: application/json, text/event-stream`,
plus **SSE parsing** and **JSON parsing**. No local snapshot store, no
event/task reminder timer, no Nextcloud UI screen.

---

# PART 2 — Recommended contract

## 2.1 The tool

Add one structured tool to the MCP server:

```text
sync_nextcloud_agenda(start_date=null,
                      days=7,
                      include_overdue_tasks=true,
                      include_undated_tasks=false,
                      task_lists=null,
                      event_calendars=null)
```

Rules:

- `start_date=null` → current date in `NC_TIMEZONE` at execution. Explicit is `YYYY-MM-DD`.
- `days` defaults to `7`, bounded `1..31`.
- `task_lists=null` / `event_calendars=null` → all compatible CalDAV collections.
- Filters accept exact display names or CalDAV slugs.

It runs one CalDAV `calendar-query` over the whole 7-day range (not seven
requests). Local range bounds are converted to UTC for the CalDAV `time-range`.

## 2.2 Output JSON

The tool returns JSON. Over MCP the value still arrives inside a `content` text
block (see §1.1), so the appliance extracts `result.content[0].text` and then
JSON-parses it. Field names match the server's existing parsed data (§1.3).

```json
{
  "schema_version": 1,
  "snapshot_id": "2026-08-04T19:00:00Z/uuid",
  "generated_at": "2026-08-04T19:00:00Z",
  "timezone": "America/Recife",
  "range": {
    "start_local": "2026-08-04T00:00:00-03:00",
    "end_local_exclusive": "2026-08-11T00:00:00-03:00",
    "days": 7
  },
  "events": [
    {
      "key": "Personal/event-uid/2026-08-05T14:00:00Z",
      "uid": "event-uid",
      "recurrence_id": "2026-08-05T14:00:00Z",
      "calendar": "Personal",
      "title": "Meeting",
      "description": "",
      "location": "",
      "start": "2026-08-05T14:00:00Z",
      "end": "2026-08-05T15:00:00Z",
      "all_day": false,
      "status": "CONFIRMED",
      "reminder_minutes_before": 10,
      "last_modified": "2026-08-01T10:00:00Z"
    }
  ],
  "tasks": [
    {
      "key": "TrabalhoFNDE/task-uid",
      "uid": "task-uid",
      "list": "TrabalhoFNDE",
      "title": "Review document",
      "description": "",
      "due": "2026-08-06T17:00:00Z",
      "status": "NEEDS-ACTION",
      "percent_complete": 0,
      "priority": null,
      "categories": [],
      "overdue": false,
      "reminder_minutes_before": 0,
      "last_modified": "2026-08-02T09:00:00Z"
    }
  ],
  "counts": { "events": 1, "tasks": 1, "overdue_tasks": 0, "undated_tasks": 0 },
  "complete": true,
  "errors": []
}
```

Field notes:

- All instants are **UTC** ISO-8601 with `Z`. `range.*` carry the local offset so
  the appliance can display without a full tz database.
- `key` is the **stable identity** used for upsert/delete:
  - event: `calendar/uid/recurrence_id` (recurring) or `calendar/uid/start` (single).
  - task: `list/uid`. Keep raw `uid` too, so a move between lists is recognizable
    as the same logical task.
- `reminder_minutes_before` is derived from the item's `VALARM` when present; the
  appliance falls back to a default (e.g. 10 min) only when it is absent/0.

## 2.3 Snapshot completeness

- `complete=true` means **every requested collection was read successfully**, so
  the snapshot is authoritative: the appliance may delete local records that
  disappeared from Nextcloud.
- If any collection fails: `complete=false`, add a bounded entry to `errors[]`,
  and the appliance **must retain** unseen local records.

## 2.4 Calendar semantics

- Include an event when it **overlaps** the range, not only when it starts in it
  (an event beginning before midnight and ending inside the range counts).
- Recurring events expanded **only** inside the requested interval.
- All-day `DTEND` is **exclusive**: Aug 4→5 is `DTSTART=20260804`, `DTEND=20260805`.
- `STATUS:CANCELLED` instances: skip, or emit as tombstones if the appliance does
  incremental sync.

## 2.5 Task semantics

Split incomplete tasks into explicit groups:

1. Due inside the range.
2. Overdue before range start, when `include_overdue_tasks=true`.
3. No due date, when `include_undated_tasks=true`.

Do not silently mix completed tasks into the active snapshot. Task identity is
`(list, uid)`; retain raw `uid` for move detection. Hide/label events carrying
`X-SERVITOR-TASK-UID` to avoid duplicating a task as an event.

---

# PART 3 — Appliance consumption model

## 3.1 Transport

- One HTTP **POST** to `/mcp`, headers `Content-Type: application/json` and
  `Accept: application/json, text/event-stream`.
- Body is JSON-RPC `tools/call` with `name: "sync_nextcloud_agenda"`.
- For long-term safety, do `initialize` then `tools/list` before `tools/call`
  (the stateless endpoint also accepts a direct `tools/call`).
- **Parse SSE**: read `event:`/`data:` lines, take the JSON from each `data:`.
- Match the JSON-RPC `id`; inspect `result.isError`; extract the payload from
  `result.content[0].text` and JSON-parse it. **Never** treat HTTP 200 alone as
  success.
- Separate timeouts: connect, whole-request, and a max response size.

## 3.2 New HTTP client needed

`hw_http_get` (`hal_interface.cpp:3598`) cannot do this (GET only, no headers, no
auth, truncating fixed buffer). Add a POST-capable helper that sets the two
headers, streams the SSE body into a large **PSRAM** buffer (`ps_malloc`), and
returns the raw body for SSE+JSON parsing. Keep it behind a thin interface (per
project convention) and provide a sim stub.

## 3.3 Local snapshot store (flash) + transaction

> **Payload size (measured):** a real busy week returned a **~35 KB** inner
> snapshot (55 events + 9 tasks). The firmware allocates the SSE and JSON scratch
> buffers from **PSRAM** (`ps_malloc`, 64 KB + 48 KB) so they never squeeze the
> internal heap, and stores up to 128 items. If bandwidth/RAM ever bites, add a
> lean tool option that omits `description`/`location` — the appliance list does
> not use them.

Persist parsed events/tasks so the watch works offline between syncs (FFat file
or NVS blob). On a successful `complete=true` snapshot:

1. Mark existing records in scope as unseen.
2. Upsert every event/task by its stable `key`; mark seen.
3. Delete/archive records still unseen.
4. Record `snapshot_id`, `generated_at`, and the sync time.

On network/parse/incomplete failure: keep the last snapshot, do not erase unseen
records, retry with bounded backoff, and expose the age of the last good sync in
the UI.

## 3.4 Reminder timer

A periodic LVGL timer compares each stored item's `start`/`due` (minus its
`reminder_minutes_before`, default 10) against `hw_get_date_time` + `mktime`.
When the lead window is reached and the item has not yet fired, raise the
existing `ui_alarm_ring()` overlay (sound + vibrate). Keep a per-item "notified"
flag to avoid repeats.

## 3.5 Config (compile-time)

Put the MCP endpoint and timezone in a compile-time header, the same way NTP
servers live in `factory.ino` — no on-watch typing, matches the Arduino-IDE
flashing flow. Use the **LAN** address `http://192.168.0.24:8001/mcp` (the watch
is not on the WireGuard VPN, so the `10.66.66.16` VPN address will not resolve
for it). **No Nextcloud credentials on the device.**

## 3.6 UI

New `ui_nextcloud.cpp`: a Sync button (spawns the fetch task like weather),
a status line (last sync age / errors), and a scrollable list of upcoming items.
Register a launcher cell with `create_app_cell(...)` (`ui_main.cpp:153`).

---

# PART 4 — Security

- The MCP server listens on `0.0.0.0:8001` with **no auth**. Reach it only over a
  trusted LAN/VPN until TLS + auth are added at a reverse proxy. Never expose
  8001 to the public internet.
- Nextcloud credentials stay on the server; the appliance never stores
  `NC_APP_PASSWORD`.
- Before any untrusted-network deployment add: TLS to the MCP endpoint,
  appliance authentication, authorization limited to read-only sync,
  replay/rate limits, bounded request/response sizes, and audit logs that omit
  task descriptions and credentials.

---

# PART 5 — Implementation checklist

**Server (ServitorAssistant MCP):** — implemented in `stream2.py` + `nextcloud_tasks.py`
- [x] Add `sync_nextcloud_agenda` returning the §2.2 JSON (`stream2.py`).
- [x] One 7-day `calendar-query` with `<c:expand>` recurrence expansion
      (reuses `_calendar_event_query`).
- [x] Split tasks into due / overdue / undated groups (`_classify_snapshot_task`).
- [x] Hide linked reminder events (`X-SERVITOR-TASK-UID`) from the events list.
- [x] Return `complete=false` + bounded `errors[]` on partial collection failure.
- [x] `reminder_minutes_before` derived from each item's VALARM (`_alarm_minutes_by_uid`).
- [x] Tests: range/overdue/undated grouping, day bounds, VALARM trigger parsing
      (`api/tests/test_nextcloud_tasks.py`, 28 pass). Verified live: 55 events +
      9 tasks, `complete=true`.
- [ ] Extend tests: DST boundaries, all-day exclusivity, recurring expansion,
      moved tasks, cancellation tombstones.
- [ ] Add MCP auth/TLS before any untrusted deployment.

**Appliance (this firmware):** — implemented
- [x] POST-capable HTTP client with SSE-framed body (`hw_mcp_post`, `hal_interface.cpp`).
- [x] JSON parser + `result.content[].text` extraction (`ui_nextcloud.cpp`);
      PSRAM scratch buffers.
- [x] Local snapshot store on FFat (`hw_fs_write_file`/`hw_fs_read_file`,
      `/nc_agenda.json`); reloaded on app open and after sync.
- [x] Reminder timer (persists across screens) raising a full-screen overlay +
      `hw_vibrate_max()`; UTC↔local via compile-time TZ offset.
- [x] `ui_nextcloud.cpp` (Sync button + status + list) + launcher cell "Agenda".
- [x] Compile-time config header (`nextcloud_config.h`); no NC credentials.
- [ ] Upsert/delete transaction against a durable store (today it replaces the
      in-RAM item set each sync; `complete=false` handling is TODO).
- [ ] Fire reminders from boot without opening the app once (persistent timer is
      created on first open today).

---

## References

- RFC 5545 — iCalendar (`VEVENT`, `VTODO`, `VALARM`, recurrence, all-day end exclusivity).
- RFC 4791 — CalDAV access and `calendar-query`.
- MCP — Streamable HTTP transport, JSON-RPC tool calls.
- ServitorAssistant source: `api/mcp_module/stremable_http/stream2.py`,
  `api/mcp_module/stremable_http/nextcloud_tasks.py`.
