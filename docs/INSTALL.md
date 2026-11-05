# Installing Road Trip Shift

Version **0.3.1 experimental**. This guide is also included in the installation ZIP. In the repository: [README](../README.md) · [Русская версия](INSTALL_RU.md).

## Requirements

Windows x64 and Road Trip: Ford. Tested on ATS **1.62.1.1**. Game addresses are found automatically from instruction signatures; there is no version or EXE hash whitelist. Other game builds have not been tested. Multiplayer is untested. Base behavior was road-tested in 0.2.2; keyboard accelerator input, signature resolution and the new 0.3.1 cruise protection still need driving validation.

## Manual installation

1. Close ATS normally. Download and extract the installation ZIP; do not copy the ZIP into `mod`.
2. In Steam, choose ATS → Manage → Browse local files. This opens `path_to_game`.
3. Copy the ZIP's `bin` directory into that game directory, merging folders. Create `plugins` if necessary. The resulting layout is:

```text
path_to_game/
  bin/win_x64/
    amtrucks.exe
    plugins/
      road_trip_shift.dll
      road_trip_shift/
        road_trip_shift.ini
```

The DLL alone is sufficient: missing INI values use built-in defaults. For an update, keep your existing INI. Retain the ZIP's `licenses` directory with any copy you redistribute. The plugin uses static compiler runtimes; no separately supplied compiler DLLs are required.

4. Start ATS and accept its SDK confirmation. Select a supported Ford, an automatic gearbox and the desired adaptive transmission mode. Double accelerator presses on a keyboard use the game's Smart Throttle. The plugin follows the accelerator action assigned in ATS automatically, without a separate key binding.

There is no Mod Manager activation for this DLL. Never replace `amtrucks.exe`.

## Optional PowerShell installer

From the extracted ZIP directory, with PowerShell 7:

```powershell
pwsh -File ./Install.ps1 -GameRoot 'path_to_game'
```

The installer refuses a running game, verifies the DLL hash, backs up any previous DLL and preserves the existing INI. Its `installation.json` file records installation details under `plugins/road_trip_shift/`, alongside backups. Profiles and other plugins are not changed.

## Settings

Edit `bin/win_x64/plugins/road_trip_shift/road_trip_shift.ini` while ATS is closed, then restart:

| Section / key | Default | Meaning |
| --- | --- | --- |
| `plugin / enabled` | `1` | `0` disables plugin behavior |
| `input / double_tap_ms` | `300` | Maximum press interval in ms; clamped to 100–700 |
| `input / analog_kickdown` | `1` | Main analog pedal/trigger double-tap kickdown |
| `input / controls_file` | empty | Optional controls.sii override; empty enables automatic active-profile discovery |
| `cruise / normal_acceleration` | `1` | Economy enters Normal beyond 3 km/h deficit, holds until target; cancels on override |
| `cruise / anti_hunt` | `1` | `0` disables new cruise gear-hunting protection for comparison |
| `logging / enabled` | `1` | `0` disables diagnostic file/status writes and this plugin's SDK log messages |
| `logging / max_total_mb` | `50` | Total event/CSV storage budget in MiB; clamped to 1–1024 |
| `logging / sample_ms` | `200` | Telemetry interval in ms; clamped to 100–2000 |

Keyboard kickdown follows the active profile accelerator binding in controls.sii, with no hardcoded W fallback. Double-tap, hold, release and pause/brake cancellation use the main branch policy. Fresh SDK throttle is diagnostic and does not confirm or gate keyboard presses. Per-press SDK confirmation and Smart Throttle ramp handling are deferred in the [implementation plan](IMPLEMENTATION_PLAN.md). Main analog kickdown is retained: double-pressing a pedal/trigger temporarily requests High Power; input/analog_kickdown=0 disables it. The plugin does not synthesize full analog throttle. The default double-tap interval is 300 ms.

The plugin reads `mix dforward` in the active profile's `controls.sii`. It supports letters, number-row keys, F1–F12, arrows, space, tab, backspace, Esc, separate Shift/Ctrl/Alt keys, navigation and common punctuation keys. Simple alternative bindings joined by OR are supported, including controller alternatives which are ignored. Complex expressions, modifiers combined with AND, unknown aliases, Enter and numpad bindings are refused rather than guessed. Windows virtual-key polling does not reliably distinguish the physical Enter/numpad variants. Unsupported bindings leave keyboard kickdown disabled. Controller software that emulates keyboard events is treated as keyboard input.

The profile is identified from the latest profile path in the current game log, under the Windows Documents game directory or the game's `-homedir` override. `input / controls_file` can explicitly select `path_to_file/controls.sii` if discovery is unavailable; relative paths are resolved against that game-home directory. Bindings are rechecked about once per second and after activity/control changes. The game must save the changed assignment before it can be picked up. Neither the log nor controls file is modified.

## Logs and troubleshooting

With logging enabled, the plugin creates `plugins/road_trip_shift/status.json` and `plugins/road_trip_shift/logs/` next to the DLL. Logs contain vehicle/input/gearbox telemetry and no network upload. Review files before sharing; the game's own log and the installer's `installation.json` file may include local paths.

The default **50 MiB** budget applies to the plugin's recognized `.log` and `.csv` files together, including earlier unsegmented sessions. Files rotate in parts of up to 5 MiB, with a new header in each CSV. On startup and rotation, the oldest completed files are removed to make space. The active part is closed before cleanup. Unrelated files, the game's own log and installation backups are outside this budget and are not deleted. If old files cannot be removed, disk logging stops rather than continuing to grow. `status.json` stays small because it is overwritten. Disabling logging preserves existing files and does not disable the gearbox features.

- `hooks_ready` indicates hook installation; `vehicle_supported` indicates vehicle recognition. `reader_calls` and `shadow_reads` confirm actual mode-reader activity. These do not alone prove correct driving behavior.
- `MEMORY source=process` records private committed memory, current/peak working set, change from the first valid sample, and handle count for the whole ATS process. `plugin_thunk_*` separately tracks active/retained hook blocks, allocations, frees and failed frees; it does not measure every CRT heap allocation. CSV/status include snapshot tick/validity and current process/hook bytes. Sampling is every 10 seconds by default; set `[logging] memory_sample_ms=10000` (1000–60000 ms). Existing INI files need no changes. Compare repeated warmed-up routes using the [Bronco test cases](https://github.com/SaveljevRoman/Road_Trip_Shift/blob/fix/hook-reinit-throttle-key-ci/docs/TESTING_BRONCO.md).
- `INPUT_GATE` records the selection/cancellation reason, physical key state, SDK validity, throttle, brake and tap state. CSV/status add `keyboard_held`, `input_brake`, `tap_armed`, `tap_blocked`, `analog_down` and `input_gate`; CSV also includes `input_sample_valid`. `KEYBOARD_BINDING` includes physical scan codes (for example, W is `0011` and S is `001F`), without profile paths.
- `GEAR_CHOICE source=game_logic` records the stock candidate and plugin return separately for `adjacent_limited` and `direct` native callers. These are candidates, not proof of the final target; the adjacent caller can limit a choice after the hook returns. `GEAR_CHANGED source=game_telemetry` records the actual engaged gear. This diagnostic build does not change that native stepping policy.
- `keyboard_binding_ready` and `input_sample_valid` in status show whether a binding and fresh accelerator sample are available. `KEYBOARD_BINDING source=controls_sii` records readiness without local paths.
- CSV `input_down` replaces `w_down` and describes the qualified physical accelerator-key state; `THROTTLE DOWN/UP source=game_input` records its edges. Only keys from the keyboard accelerator binding are polled.
- `CONFIG_MODE_CHANGED source=game_setting` records a game setting change, including profile loads. `KICKDOWN ON/OFF source=plugin` records a temporary plugin request. Already configured High Power can produce `value_changed=0`.
- With Economy selected, cruise starts temporary Normal recovery only when speed is more than 3 km/h below its current target. Once active, recovery continues through the 3 km/h band and returns to Economy at the target; it does not wait for a 3 km/h overshoot. This target-relative hysteresis applies to target increases, later speed loss and recovery after ACC braking. Manual gas, kickdown, braking, unavailable input, pause or cruise cancellation cancels recovery; after cancellation the entry band applies again. Small target changes inside the band do not repeatedly activate Normal. Lowering the target stops recovery when that target is reached. The current plugin Normal hooks and cruise guard remain active, High Power kickdown has priority, and the saved game setting is never written. Diagnostics use `CRUISE_ACCEL source=plugin`, `mode_source=cruise_acceleration` and `cruise_acceleration_normal`. Disable using `[cruise] normal_acceleration=0`.
- `CRUISE_GUARD source=cruise_load`, `cruise_guard_cap` and `cruise_guard_clamps` describe failed-upshift protection. `effective_brake` helps distinguish ACC braking from lack of power.
- If hooks are not applied, check the log for missing or ambiguous instruction patterns and overlapping transmission plugins.
- Compare a single accelerator press, double press/release and cruise at several speeds and loads. A selected top gear is not a success criterion; the car must still maintain speed. Adaptive cruise can slow down for traffic.


## Native HUD and engine diagnostics

The PR test build requests [native HUD mode notifications](MODE_NOTICE.md). Defaults are `[hud] enabled=1`, `duration_ms=2500`, `language=auto` (Windows UI language; explicit `en`/`ru` supported). Existing INI files remain valid. Game notification visibility settings still apply. Native HUD signature failure disables only the indicator; `MODE_NOTICE READY/REQUEST/EXPIRE_REQUEST` records availability/requests without proving rendering. Visual driving validation is pending.

Optional engine diagnostics add CSV columns `engine_enabled`, `motor_brake`, `parking_brake`, `electric_enabled`, `retarder_level`, `input_clutch`, `effective_clutch`, `engine_wear` and `engine_signals_valid`. Values of -1 mean unavailable, never a confirmed off/zero state. Validity-mask bits 0–7 follow that order. `ENGINE_STATE source=game_telemetry` records initial availability, digital/retarder changes and clutch zero/nonzero edges immediately in SDK callbacks, with speed/RPM/gear/throttle/brake context; CSV samples continuous clutch/wear values. Channel registration is optional and failures do not disable the transmission. Vehicle changes reset validity. The SDK has no direct fuel-cut/rev-limiter flag: these signals can distinguish engine enable/braking/clutch events, but cannot by themselves prove a brief limiter cut.

## Removal

### Nanosecond timing

`[logging] timing=1` enables QPC duration profiling by default. `timing_sample_ms=10000` controls report cadence (1000–60000 ms); `timing_hook_every=64` controls native-reader/chooser sampling (1–4096). Existing INI files need no edits. Set `timing=0` before launching for a comparison without profiling; `logging/enabled=0` also disables it. Durations and the appended event/CSV `qpc_ns` timestamp are nanoseconds relative to the first QPC initialization in this loaded DLL, not UTC. `qpc_ns=0` means profiling is disabled/unavailable or at the origin.

`PERF_CLOCK` gives the actual QPC frequency and nominal tick size. `PERF` counters are cumulative over this loaded DLL's lifetime, including SDK reinitialization: sampled calls, total/average/maximum duration and counts at least 100 microseconds, 1 ms and 5 ms. Hook counts are measured samples, not total native calls. Other sections measure callbacks including paused/menu work. `frame_start`/`frame_end` are DLL callback durations, not total game-frame time/FPS. Parent sections include child sections and instrumentation, so do not sum them. Native totals include the stock reader/chooser; their stock-only sections help separate the game's work. Profiling itself has overhead, and sampled maxima can miss unsampled stalls. Timing alone does not establish the cause of a shift or prove the DLL is slow.

Close ATS and delete only `bin/win_x64/plugins/road_trip_shift.dll`. The matching `road_trip_shift` directory can also be removed if you no longer need its settings, logs or backups. Never delete the whole `plugins` folder. Process-memory changes disappear when ATS exits.

## Keyboard kickdown priority

A double press of the assigned keyboard accelerator has the highest transmission-mode priority: temporary High Power overrides Off, Economy, Normal, already selected High Power, custom values, cruise holding/recovery and both cruise options. Entering kickdown immediately clears pending Normal recovery; recovery cannot re-enable Normal while kickdown is held. Changing the selected game mode during the hold does not interrupt High Power. Release resumes the current selection/recovery policy, without saving or overwriting the game setting. As in main, full-throttle acceleration comes from the game's Smart Throttle and native gearbox maps; the DLL does not synthesize throttle or assign a numbered gear. Input validity, supported-vehicle scope, plugin enable and cancellation checks still apply.


Current feature selection and deferred work: [implementation plan](IMPLEMENTATION_PLAN.md).

CRUISE_ACCEL transition diagnostics include six-decimal target/speed/error, transition count and interval, input/effective throttle, both brakes, SDK availability, keyboard/kickdown, scope/focus, normal option, guard cap, gear and RPM. CSV target/speed also use six decimals. Recovery now enters beyond a 3 km/h deficit and exits at the target; repeated small target crossings remain Economy. See the implementation plan.
