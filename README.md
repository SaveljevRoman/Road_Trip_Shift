# Road Trip Shift

[Русская версия](README_RU.md)

An experimental Windows x64 plugin for **American Truck Simulator — Road Trip: Ford**, addressing the automatic gearbox's different behavior under cruise control and manual acceleration.

**Current build: 0.3.2 experimental**, tested on ATS 1.62.1.1 (Windows x64). Base kickdown/mode behavior was road-tested in 0.2.2. Keyboard accelerator input, signature resolution and the new cruise gear-hunting protection pass automated checks but still need validation in a new drive. This unofficial project is unaffiliated with SCS Software or Ford.

## The problem this addresses

On affected Ford cars, choosing an economical transmission mode can give relaxed shifts while accelerating manually, yet enabling cruise can make the gearbox downshift and hold higher RPM as though a more aggressive mode were active. This can happen even after reaching the cruise setpoint, when the car only needs to maintain speed.

Road Trip Shift makes the selected transmission mode available to the gearbox during cruise as well as single-press acceleration. It preserves the game's engine characteristics and shift maps. Double-tap acceleration remains available as an explicit, temporary request for High Power, with the selected mode restored on release.

A second problem can appear under load: the gearbox upshifts, cannot sustain speed, downshifts to recover, then repeats the same failed upshift. The experimental cruise guard observes that failure and temporarily delays another attempt. It does not insist on top gear or a fixed RPM; ordinary downshifts remain available for hills and other loads.

## Download and install

- **[Installation ZIP](release/road_trip_shift_0.3.2_win64.zip)** — DLL, optional settings, installer, EN/RU instructions and third-party notices.
- [DLL alone](release/road_trip_shift.dll) · [SHA-256 checksums](release/SHA256SUMS.txt) · [Build details](release/BUILD-INFO.json)
- **[Installation guide](docs/INSTALL.md)** · **[Установка на русском](docs/INSTALL_RU.md)**

Close ATS, then copy `road_trip_shift.dll` into `path_to_game/bin/win_x64/plugins/`. Create `plugins` if necessary. The DLL works with built-in defaults; the ZIP also supplies an editable INI. Installation does not use the Mod Manager. Steam's **Manage → Browse local files** opens `path_to_game`.

## Controls

| Input | Transmission mode |
| --- | --- |
| Cruise holding speed | Selected in game settings |
| Economy cruise more than 3 km/h below target | Temporary Normal until target is reached |
| Single accelerator press | Selected mode |
| Double keyboard accelerator press, holding the second press | Temporary High Power |
| Release the keyboard accelerator | End High Power; selected mode or pending cruise recovery resumes |

Keyboard kickdown follows the active profile accelerator binding in controls.sii, with no hardcoded W fallback. Double-tap, hold, release and pause/brake cancellation use the main branch policy. Fresh SDK throttle is diagnostic and does not confirm or gate keyboard presses. Per-press SDK confirmation and Smart Throttle ramp handling are deferred in the [implementation plan](docs/IMPLEMENTATION_PLAN.md). Main analog kickdown is retained: double-pressing a pedal/trigger temporarily requests High Power; input/analog_kickdown=0 disables it. The plugin does not synthesize full analog throttle. The default double-tap interval is 300 ms.

Automatic binding discovery uses the latest profile path in the current `game.log.txt`, supports local/Steam profiles, redirected Documents and `-homedir`. Missing or unsupported bindings disable keyboard kickdown while cruise behavior stays available. See the installation guide for supported keys and the optional `controls_file` override.

With Economy selected, cruise starts temporary Normal recovery only when speed is more than 3 km/h below its current target. Once active, recovery continues through the 3 km/h band and returns to Economy at the target; it does not wait for a 3 km/h overshoot. This target-relative hysteresis applies to target increases, later speed loss and recovery after ACC braking. Manual gas, kickdown, braking, unavailable input, pause or cruise cancellation cancels recovery; after cancellation the entry band applies again. Small target changes inside the band do not repeatedly activate Normal. Lowering the target stops recovery when that target is reached. The current plugin Normal hooks and cruise guard remain active, High Power kickdown has priority, and the saved game setting is never written. Diagnostics use `CRUISE_ACCEL source=plugin`, `mode_source=cruise_acceleration` and `cruise_acceleration_normal`. Disable using `[cruise] normal_acceleration=0`.

Cruise protection observes a failed higher gear under load and a subsequent stock downshift, then temporarily prevents another attempt at that higher gear. It preserves downshifts and resets on manual acceleration, kickdown or braking. It does not force top gear, guarantee an RPM or use a fixed road speed. Adaptive cruise still controls speed and braking around traffic.

A double press of the assigned keyboard accelerator has the highest transmission-mode priority: temporary High Power overrides Off, Economy, Normal, already selected High Power, custom values, cruise holding/recovery and both cruise options. Entering kickdown immediately clears pending Normal recovery; recovery cannot re-enable Normal while kickdown is held. Changing the selected game mode during the hold does not interrupt High Power. Release resumes the current selection/recovery policy, without saving or overwriting the game setting. As in main, full-throttle acceleration comes from the game's Smart Throttle and native gearbox maps; the DLL does not synthesize throttle or assign a numbered gear. Input validity, supported-vehicle scope, plugin enable and cancellation checks still apply.

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

[Changelog and review starting points](docs/CHANGELOG.md) · [Official SCS SDK references](docs/SDK_REFERENCES.md)

[Diagnostic reports and latest handoff](debug/README.md)

## Mode notification and engine diagnostics

The PR test build includes a [native HUD mode notification](docs/MODE_NOTICE.md): grey Off, green Eco, blue Normal and red High Power on actual setting changes, approximately 2.5 seconds. It uses the game's own notification system and needs no extra application. Native address/ABI checks pass; appearance and timing still require in-game validation. Existing INI files need no edits.

Optional engine diagnostics add CSV columns `engine_enabled`, `motor_brake`, `parking_brake`, `electric_enabled`, `retarder_level`, `input_clutch`, `effective_clutch`, `engine_wear` and `engine_signals_valid`. Values of -1 mean unavailable, never a confirmed off/zero state. Validity-mask bits 0–7 follow that order. `ENGINE_STATE source=game_telemetry` records initial availability, digital/retarder changes and clutch zero/nonzero edges immediately in SDK callbacks, with speed/RPM/gear/throttle/brake context; CSV samples continuous clutch/wear values. Channel registration is optional and failures do not disable the transmission. Vehicle changes reset validity. The SDK has no direct fuel-cut/rev-limiter flag: these signals can distinguish engine enable/braking/clutch events, but cannot by themselves prove a brief limiter cut.

## Credits

High-resolution profiling is enabled by default when diagnostics are enabled. Events and CSV include `qpc_ns` elapsed from the DLL's first QPC initialization; `PERF_CLOCK` records frequency/resolution and `PERF` reports cumulative sampled durations in nanoseconds, counts, averages, maxima and calls at least 100 microseconds/1 ms/5 ms. Sections distinguish DLL callbacks, input/bindings, cruise, HUD, memory, logging/CSV/status and native reader/chooser calls. Key-binding file reads run on a background thread, outside `frame_end`. Native hook paths are sampled every 64 calls by default. Nanoseconds are an output unit, not a promise of one-nanosecond accuracy. See [settings and interpretation](docs/INSTALL.md).

Uses the official [SCS Telemetry & Input SDK](https://modding.scssoft.com/wiki/Documentation/Engine/SDK/Telemetry). Hook approaches were studied in [car_automatic_transmission](https://github.com/deppelover/car_automatic_transmission) and [DazDrive](https://github.com/DamienDutch/dazdrive). See [third-party provenance and notices](third_party/README.md); their prebuilt plugins are not included.


Current feature selection and deferred work: [implementation plan](docs/IMPLEMENTATION_PLAN.md).
