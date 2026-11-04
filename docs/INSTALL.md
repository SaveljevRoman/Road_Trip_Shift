# Installing Road Trip Shift

Version **0.3.1 experimental**. This guide is also included in the installation ZIP. In the repository: [README](../README.md) · [Русская версия](INSTALL_RU.md).

## Requirements

Windows x64 and Road Trip: Ford. Tested on ATS **1.62.1.1**. Game addresses are found automatically from instruction signatures; there is no version or EXE hash whitelist. Other game builds have not been tested. Multiplayer is untested. Base behavior was road-tested in 0.2.2; the new 0.3.1 cruise protection still needs driving validation.

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

4. Start ATS and accept its SDK confirmation. Select a supported Ford, an automatic gearbox and the desired adaptive transmission mode. Keyboard double-W acceleration uses the game's Smart Throttle.

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
| `input / analog_kickdown` | `1` | Enable analog pedal double-press detection |
| `cruise / anti_hunt` | `1` | `0` disables new cruise gear-hunting protection for comparison |
| `logging / enabled` | `1` | `0` disables diagnostic file/status writes and this plugin's SDK log messages |
| `logging / max_total_mb` | `50` | Total event/CSV storage budget in MiB; clamped to 1–1024 |
| `logging / sample_ms` | `200` | Telemetry interval in ms; clamped to 100–2000 |

Double W selects temporary High Power while the second press is held; release returns to the current configured mode. Single W and cruise retain that mode. An analog double press changes mode without synthesizing full pedal input.

## Logs and troubleshooting

With logging enabled, the plugin creates `plugins/road_trip_shift/status.json` and `plugins/road_trip_shift/logs/` next to the DLL. Logs contain vehicle/input/gearbox telemetry and no network upload. Review files before sharing; the game's own log and the installer's `installation.json` file may include local paths.

The default **50 MiB** budget applies to the plugin's recognized `.log` and `.csv` files together, including earlier unsegmented sessions. Files rotate in parts of up to 5 MiB, with a new header in each CSV. On startup and rotation, the oldest completed files are removed to make space. The active part is closed before cleanup. Unrelated files, the game's own log and installation backups are outside this budget and are not deleted. If old files cannot be removed, disk logging stops rather than continuing to grow. `status.json` stays small because it is overwritten. Disabling logging preserves existing files and does not disable the gearbox features.

- `hooks_ready` indicates hook installation; `vehicle_supported` indicates vehicle recognition. `reader_calls` and `shadow_reads` confirm actual mode-reader activity. These do not alone prove correct driving behavior.
- `CONFIG_MODE_CHANGED source=game_setting` records a game setting change, including profile loads. `KICKDOWN ON/OFF source=plugin` records a temporary plugin request. Already configured High Power can produce `value_changed=0`.
- `CRUISE_GUARD source=cruise_load`, `cruise_guard_cap` and `cruise_guard_clamps` describe failed-upshift protection. `effective_brake` helps distinguish ACC braking from lack of power.
- If hooks are not applied, check the log for missing or ambiguous instruction patterns and overlapping transmission plugins.
- Compare single W, double W/release and cruise at several speeds and loads. A selected top gear is not a success criterion; the car must still maintain speed. Adaptive cruise can slow down for traffic.

## Removal

Close ATS and delete only `bin/win_x64/plugins/road_trip_shift.dll`. The matching `road_trip_shift` directory can also be removed if you no longer need its settings, logs or backups. Never delete the whole `plugins` folder. Process-memory changes disappear when ATS exits.
