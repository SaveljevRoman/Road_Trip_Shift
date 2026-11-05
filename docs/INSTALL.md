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
| `input / controls_file` | empty | Optional controls.sii override; empty enables automatic active-profile discovery |
| `cruise / normal_acceleration` | `1` | Economy uses current plugin Normal for any active cruise speed deficit; restore at target/cancellation |
| `cruise / anti_hunt` | `1` | `0` disables new cruise gear-hunting protection for comparison |
| `logging / enabled` | `1` | `0` disables diagnostic file/status writes and this plugin's SDK log messages |
| `logging / max_total_mb` | `50` | Total event/CSV storage budget in MiB; clamped to 1–1024 |
| `logging / sample_ms` | `200` | Telemetry interval in ms; clamped to 100–2000 |

Double-press the assigned keyboard accelerator and hold the second press for temporary High Power; releasing that key ends High Power even if a controller is holding throttle; the selected-mode/cruise-recovery policy resumes. A single keyboard press and cruise holding speed retain the selected mode; the speed-recovery exception is described below. Gamepad buttons, triggers and pedals cannot activate plugin kickdown on their own. Startup, pause/resume, focus loss, timer restart and vehicle changes require a key release before another double press. No W fallback is used.

The plugin reads `mix dforward` in the active profile's `controls.sii`, then confirms keyboard input with fresh SDK throttle. It supports letters, number-row keys, F1–F12, arrows, space, tab, backspace, Esc, separate Shift/Ctrl/Alt keys, navigation and common punctuation keys. Simple alternative bindings joined by OR are supported, including controller alternatives which are ignored. Complex expressions, modifiers combined with AND, unknown aliases, Enter and numpad bindings are refused rather than guessed. Windows virtual-key polling does not reliably distinguish the physical Enter/numpad variants. Unsupported bindings leave keyboard kickdown disabled. Controller software that emulates keyboard events is treated as keyboard input.

The profile is identified from the latest profile path in the current game log, under the Windows Documents game directory or the game's `-homedir` override. `input / controls_file` can explicitly select `path_to_file/controls.sii` if discovery is unavailable; relative paths are resolved against that game-home directory. Bindings are rechecked about once per second and after activity/control changes. The game must save the changed assignment before it can be picked up. Neither the log nor controls file is modified.

Existing INI files can be kept. The obsolete `input / analog_kickdown` setting is ignored: only the configured keyboard accelerator is now eligible for plugin kickdown. The interval remains `input / double_tap_ms`.

## Logs and troubleshooting

With logging enabled, the plugin creates `plugins/road_trip_shift/status.json` and `plugins/road_trip_shift/logs/` next to the DLL. Logs contain vehicle/input/gearbox telemetry and no network upload. Review files before sharing; the game's own log and the installer's `installation.json` file may include local paths.

The default **50 MiB** budget applies to the plugin's recognized `.log` and `.csv` files together, including earlier unsegmented sessions. Files rotate in parts of up to 5 MiB, with a new header in each CSV. On startup and rotation, the oldest completed files are removed to make space. The active part is closed before cleanup. Unrelated files, the game's own log and installation backups are outside this budget and are not deleted. If old files cannot be removed, disk logging stops rather than continuing to grow. `status.json` stays small because it is overwritten. Disabling logging preserves existing files and does not disable the gearbox features.

- `hooks_ready` indicates hook installation; `vehicle_supported` indicates vehicle recognition. `reader_calls` and `shadow_reads` confirm actual mode-reader activity. These do not alone prove correct driving behavior.
- `MEMORY source=process` records private committed memory, current/peak working set, change from the first valid sample, and handle count for the whole ATS process. `plugin_thunk_*` separately tracks active/retained hook blocks, allocations, frees and failed frees; it does not measure every CRT heap allocation. CSV/status include snapshot tick/validity and current process/hook bytes. Sampling is every 10 seconds by default; set `[logging] memory_sample_ms=10000` (1000–60000 ms). Existing INI files need no changes. Compare repeated warmed-up routes using the [Bronco test cases](https://github.com/SaveljevRoman/Road_Trip_Shift/blob/fix/hook-reinit-throttle-key-ci/docs/TESTING_BRONCO.md).
- `INPUT_GATE` records the blocking/confirmation reason, physical key state, SDK validity, throttle, brake and tap state. CSV/status add `keyboard_held`, `input_brake`, `tap_armed`, `tap_blocked`, `tap_confirmed` and `input_gate`; CSV also includes `input_sample_valid`. `KEYBOARD_BINDING` includes physical scan codes (for example, W is `0011` and S is `001F`), without profile paths.
- `GEAR_CHOICE source=game_logic` records the stock candidate and plugin return separately for `adjacent_limited` and `direct` native callers. These are candidates, not proof of the final target; the adjacent caller can limit a choice after the hook returns. `GEAR_CHANGED source=game_telemetry` records the actual engaged gear. This diagnostic build does not change that native stepping policy.
- `keyboard_binding_ready` and `input_sample_valid` in status show whether a binding and fresh accelerator sample are available. `KEYBOARD_BINDING source=controls_sii` records readiness without local paths.
- CSV `input_down` replaces `w_down` and describes the qualified physical accelerator-key state; `THROTTLE DOWN/UP source=game_input` records its edges. Only keys from the keyboard accelerator binding are polled.
- `CONFIG_MODE_CHANGED source=game_setting` records a game setting change, including profile loads. `KICKDOWN ON/OFF source=plugin` records a temporary plugin request. Already configured High Power can produce `value_changed=0`.
- With Economy selected, active cruise temporarily uses the current plugin Normal behavior whenever actual speed is below the cruise target, including both target increases and later speed loss. At or above the target it returns to Economy. Manual acceleration stays Economy; manual gas, kickdown, braking/ACC braking, invalid input, pause or cruise cancellation cancels the temporary Normal state. Releasing the override allows recovery to resume if active cruise still has a speed deficit. Lowering the target ends recovery only when the new target is satisfied. The saved setting is never written. Normal and High Power retain their existing plugin behavior, and the same cruise guard/chooser path remains active during recovery. `CRUISE_ACCEL source=plugin`, `mode_source=cruise_acceleration` and `cruise_acceleration_normal` expose the temporary state. Disable using `[cruise] normal_acceleration=0`. Traffic and adaptive-cruise slowdowns are included: while ACC brakes, its intervention remains authoritative; once braking ends and cruise can accelerate, any remaining deficit uses the same Normal recovery without requiring a target change. A keyboard double tap while cruise is active requests High Power with priority over Normal recovery. Releasing the key ends High Power: recovery resumes if still below target, otherwise Economy applies. The saved Economy setting never changes.
- `CRUISE_GUARD source=cruise_load`, `cruise_guard_cap` and `cruise_guard_clamps` describe failed-upshift protection. `effective_brake` helps distinguish ACC braking from lack of power.
- If hooks are not applied, check the log for missing or ambiguous instruction patterns and overlapping transmission plugins.
- Compare a single accelerator press, double press/release and cruise at several speeds and loads. A selected top gear is not a success criterion; the car must still maintain speed. Adaptive cruise can slow down for traffic.


## Removal

Close ATS and delete only `bin/win_x64/plugins/road_trip_shift.dll`. The matching `road_trip_shift` directory can also be removed if you no longer need its settings, logs or backups. Never delete the whole `plugins` folder. Process-memory changes disappear when ATS exits.

## Keyboard kickdown priority

A confirmed double press of the assigned keyboard accelerator has the highest transmission-mode priority: temporary High Power overrides Off, Economy, Normal, already selected High Power, custom values, cruise holding/recovery and both cruise options. Entering kickdown immediately clears pending Normal recovery; recovery cannot re-enable Normal while kickdown is held. Changing the selected game mode during the hold does not interrupt High Power. Release resumes the current selection/recovery policy, without saving or overwriting the game setting. As in main, full-throttle acceleration comes from the game's Smart Throttle and native gearbox maps; the DLL does not synthesize throttle or assign a numbered gear. Input validity, supported-vehicle scope, plugin enable and cancellation checks still apply.
