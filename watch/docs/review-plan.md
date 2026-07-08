# Review plan — `documentation` branch (session 2026-06-19)

Hand this to a reviewing agent. Scope is the stacked session work on top of
`quality-tools`.

## Diff under review

```
git diff quality-tools..documentation -- '*.cpp' '*.h'
```

13 commits, ~1040 insertions across 16 code files. Tip: `62b887d`.
Build/smoke gate: `make -C sim && SDL_VIDEODRIVER=dummy timeout 8 ./sim/sim`
(exit 124 = ran fine). Static: `./scripts/quality_report.sh`.

Note: most logic lives in `#ifdef ARDUINO` blocks that the sim does **not**
exercise (codec, FreeRTOS tasks, FFat, PMU). Sim only proves it compiles and
the UI builds. Real risk is on-hardware behavior — review the ARDUINO paths by
reading, not by trusting the smoke test.

## Priority 1 — voice recorder (`62b887d`, highest risk, brand new)

`hal_interface.cpp` recorder block + `playWAV` + `hw_sd_play` `.wav` branch;
`ui_recorder.cpp`; power-key change in `power_key_event_cb`.

Check:
- **Task lifecycle / races.** `recordTask` sets `recTaskHandler=NULL` then
  `vTaskDelete(NULL)`. `hw_record_stop()` spins on `recTaskHandler` while
  `delay(10)`. Confirm no double-start (`hw_record_start` guard), no leak if
  `xTaskCreate` fails, no use-after-free of `recPath` (String) across threads.
- **`goto cleanup` correctness.** Jumps must not cross non-trivial inits into
  scope. `File f` / `bool opened` declared before any goto; WAV-write block is
  self-contained. Verify on the Arduino toolchain (sim is gcc/host).
- **Codec lifecycle.** `codec.open(16, inCh, 16000)` on start, `codec.close()`
  on every exit path including early `goto cleanup` (guarded by `opened`).
  Confirm `inCh` from `getCodecInputChannels()` and the mono downmix
  `mono[i] = in[i*inCh]` (takes channel 0) is right for the ES8311.
- **WAV header.** 44-byte little-endian PCM header in `hw_wav_fill_header`;
  placeholder written first, patched via `f.seek(0)` on stop. Verify field
  offsets/values (byteRate, blockAlign, dataLen = riffLen-36).
- **Storage guard / cap.** `REC_MIN_FREE_BYTES` stop-before-full; rolling cap
  `hw_record_enforce_cap` keeps ≤ REC_MAX_FILES-1 before a new file. Check the
  loop bound `files.size() - i >= REC_MAX_FILES` for off-by-one and unsigned
  underflow when `files.size() < i`.
- **Filename ordering.** `/rec_%010u.wav`, lexical == chronological. Confirm
  `hw_record_next_path` parses the last seq correctly and `hw_record_collect`
  normalises the leading-'/' from `File::name()` consistently (varies by core).
- **Concurrency invariant.** Record uses `codec.read`; playback uses
  `codec.write`; mic app uses `codec.read`. Confirm no path lets recording and
  playback (or mic FFT) run at once. UI gates with `hw_record_active()` /
  `hw_player_running()` — is that sufficient, or can the launcher leave one
  running on app switch?
- **`playWAV` event bits.** Mirrors `playMP3`: sets PLAYER_RUNNING, honors
  PLAYER_END via `mp3_wait_for_continue`. Runs inside `playerTask` (via
  `hw_sd_play`) — confirm single-context, and that `hw_set_play_stop()` from
  the alarm/back actually breaks the write loop.

## Priority 2 — power & sleep behavior (hardware-only)

- **`873e143` light-sleep ON by default.** `ENABLE_CLOCK_LIGHT_SLEEP` now
  `#define`d in `ui_main.cpp`. Vendor `lightSleep()` blanks the panel each
  period → expect a ~1 s clock blink. Power key always wakes. Reviewer can't
  validate on host — flag as **needs hardware sign-off**.
- **`a62c0b3` power-off overlay.** `PMU_EVENT_KEY_LONG_PRESSED` →
  `ui_power_off_show()` on `lv_layer_top`. Confirm overlay can't stack twice,
  Cancel restores, and AXP2101 4 s hard-off still fires independently.
- **power-key recorder branch.** While recording, short press only toggles
  backlight (no `lightSleep`, no power-off menu). Confirm long-press still
  reaches the power-off path while recording.

## Priority 3 — smaller functional changes

- **`3d1f35a` LoRa idle sleep.** `radio.sleep()` at boot + on app exit;
  `radio.standby()` before reconfigure. Confirm SPI lock/unlock pairing and
  that LoRa app still TX/RX after wake.
- **`145ce5d` pedometer.** BMA423 on-chip step counter; backlight dim/restore.
  Confirm restore on every exit path.
- **`cb922f8` wallpaper fix.** Screensaver + HOME both use `img_home_wallpaper`;
  old asset removed — grep for dangling refs.
- **`ab488a7` alarm picker.** `[SD]`/`[FFat]` label prefix; saved choice still
  stores raw filename (pre-select unaffected). Verify the compare path.

## Priority 4 — docs-only (low risk, skim)

`95433a6`, `a9fd692`, `590437a` add doxygen `@brief` + give enums `: uint8_t`
bases. Check the enum base changes don't alter ABI where the enum crosses a
struct/union boundary (e.g. `app_event` in `event_define.h` unions).

## Out of scope

Asset cleanup and refactors already landed under `quality-tools` and earlier
stacked branches. None of the stack is merged to `master` yet.
