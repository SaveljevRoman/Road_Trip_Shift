# Road Trip Shift

[Русская версия](README_RU.md)

An experimental Windows x64 plugin for **American Truck Simulator — Road Trip: Ford**, addressing the automatic gearbox's different behavior under cruise control and manual acceleration.

**Current build: 0.3.1 experimental**, tested on ATS 1.62.1.1 (Windows x64). Base kickdown/mode behavior was road-tested in 0.2.2. Keyboard accelerator input, signature resolution and the new cruise gear-hunting protection pass automated checks but still need validation in a new drive. This unofficial project is unaffiliated with SCS Software or Ford.

## The problem this addresses

On affected Ford cars, choosing an economical transmission mode can give relaxed shifts while accelerating manually, yet enabling cruise can make the gearbox downshift and hold higher RPM as though a more aggressive mode were active. This can happen even after reaching the cruise setpoint, when the car only needs to maintain speed.

Road Trip Shift makes the selected transmission mode available to the gearbox during cruise as well as single-press acceleration. It preserves the game's engine characteristics and shift maps. Double-tap acceleration remains available as an explicit, temporary request for High Power, with the selected mode restored on release.

A second problem can appear under load: the gearbox upshifts, cannot sustain speed, downshifts to recover, then repeats the same failed upshift. The experimental cruise guard observes that failure and temporarily delays another attempt. It does not insist on top gear or a fixed RPM; ordinary downshifts remain available for hills and other loads.

## Download and install

- **[Installation ZIP](release/road_trip_shift_0.3.1_win64.zip)** — DLL, optional settings, installer, EN/RU instructions and third-party notices.
- [DLL alone](release/road_trip_shift.dll) · [SHA-256 checksums](release/SHA256SUMS.txt) · [Build details](release/BUILD-INFO.json)
- **[Installation guide](docs/INSTALL.md)** · **[Установка на русском](docs/INSTALL_RU.md)**

Close ATS, then copy `road_trip_shift.dll` into `path_to_game/bin/win_x64/plugins/`. Create `plugins` if necessary. The DLL works with built-in defaults; the ZIP also supplies an editable INI. Installation does not use the Mod Manager. Steam's **Manage → Browse local files** opens `path_to_game`.

## Controls

| Input | Transmission mode |
| --- | --- |
| Cruise holding speed | Selected in game settings |
| Increased cruise target with Economy selected | Temporary Normal until target is reached |
| Single accelerator press | Selected mode |
| Double keyboard accelerator press, holding the second press | Temporary High Power |
| Release the keyboard accelerator | Current selected mode restored |

Kickdown follows the keyboard accelerator assigned in ATS. It reads the active profile's `controls.sii` and requires both that physical keyboard key and fresh SDK `truck.input.throttle` input. Gamepad buttons, triggers and pedals alone do not trigger plugin kickdown. There is no hardcoded W fallback. Keyboard acceleration uses the game's Smart Throttle. Startup, pause/resume, focus loss, timer restarts and vehicle changes require release before new presses. Cruise-generated effective throttle is not an input. The default double-tap interval is 300 ms, measured between physical key presses. Smart Throttle ramps up: each press must produce positive fresh SDK input within that configured interval; there is no fixed throttle-percentage threshold or separate confirmation timeout. A released, unconfirmed press cannot arm kickdown. If High Power is already selected, the request is still logged separately without changing its value.

Automatic binding discovery uses the latest profile path in the current `game.log.txt`, supports local/Steam profiles, redirected Documents and `-homedir`. Missing or unsupported bindings disable keyboard kickdown while cruise behavior stays available. See the installation guide for supported keys and the optional `controls_file` override.

When an already engaged cruise target is increased with Economy selected, the plugin temporarily supplies Normal to the gearbox for that acceleration. It restores the current selection at the new target, target decrease, braking, manual acceleration, kickdown or cruise cancellation. It does not write the game setting; Normal and High Power selections retain their own behavior. Set `[cruise] normal_acceleration=0` to disable this exception. `CRUISE_ACCEL source=plugin` distinguishes it from a user change and from kickdown.

Cruise protection observes a failed higher gear under load and a subsequent stock downshift, then temporarily prevents another attempt at that higher gear. It preserves downshifts and resets on manual acceleration, kickdown or braking. It does not force top gear, guarantee an RPM or use a fixed road speed. Adaptive cruise still controls speed and braking around traffic.

## Compatibility

- Windows x64; tested on ATS **1.62.1.1**.
- SDK vehicle IDs: `ford.f150_23`, `ford.bronco_24`, `ford.victoria_06`, `ford.mustang_67`; the SDK's `vehicle.` prefix is accepted. These identifiers may differ from the DLC's model-year labels.
- Intended for single-player. Multiplayer is untested.
- Game addresses are found automatically from instruction signatures, without a version or EXE hash whitelist. Compatibility with other game builds has not been tested.

Changes affect process memory; the game EXE on disk is unchanged. Gear ratios, torque and RPM maps remain stock.

## Bounded diagnostics

Event logs and CSV telemetry share a **50 MiB** default storage budget. Long sessions rotate into segments of at most 5 MiB; the oldest completed files are deleted when space is needed. Every CSV segment has its own header. The small `status.json` file is overwritten separately. Set `[logging] enabled=0` to disable diagnostic file/status writes and the plugin's SDK log messages; gearbox behavior stays enabled. Memory diagnostics distinguish whole-process private memory/working set/handles from tracked plugin hook allocations. See the installation guide for settings. [Bronco driving test cases](docs/TESTING_BRONCO.md).

## Documentation and source layout

| Path / guide | Purpose |
| --- | --- |
| [Building](docs/BUILD.md) / [Сборка](docs/BUILD_RU.md) | Requirements, compilation, verification and packaging |
| [Installation](docs/INSTALL.md) / [Установка](docs/INSTALL_RU.md) | Installation, settings, logs, updates and removal |
| [Engineering notes](docs/DEVELOPMENT.md) | Hooks, cruise guard, telemetry and validation limits |
| `plugin/src/road_trip_shift.cpp` | SDK entry points, telemetry, native hooks and logging |
| `plugin/src/native_scan.h`, `plugin/src/native_signatures.h` | Locate gearbox code and data automatically |
| `plugin/src/kickdown.h` | Input state machine, vehicle scope and temporary mode |
| `plugin/src/cruise_guard.h` | Cruise gear-hunting protection |
| `plugin/src/rolling_log.h` | Bounded diagnostic storage and rotation |
| `plugin/src/*tests.cpp` | Logic, native and log-retention checks |
| `plugin/build.ps1`, `plugin/Install.ps1` | Build/package and optional installer |
| `plugin/config/road_trip_shift.ini` | Default settings |
| `scripts/` | Toolchain setup, instruction and publication checks |
| `third_party/` | SDK headers, provenance and notices |
| `release/` | Latest experimental DLL, ZIP, checksums and manifest |
| `.build/` | Ignored local tests and package staging |

Before contributing, read [AGENTS.md](AGENTS.md) and [CLAUDE.md](CLAUDE.md). Keep them byte-identical and update them together. Local paths, logs, game assets, backups and toolchains stay out of Git; only deliberate distribution outputs belong in `release/`.

[Development roadmap](docs/ROADMAP.md) · [План развития](docs/ROADMAP_RU.md)

## Credits

Uses the official [SCS Telemetry & Input SDK](https://modding.scssoft.com/wiki/Documentation/Engine/SDK/Telemetry). Hook approaches were studied in [car_automatic_transmission](https://github.com/deppelover/car_automatic_transmission) and [DazDrive](https://github.com/DamienDutch/dazdrive). See [third-party provenance and notices](third_party/README.md); their prebuilt plugins are not included.
