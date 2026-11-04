# Engineering notes

[README](../README.md) · [Build](BUILD.md) · [Installation](INSTALL.md)

## Scope and hooks

The official SCS SDK 1.16 supplies vehicle identity, input and telemetry, but has no documented command for replacing adaptive transmission decisions per shift. The DLL therefore uses guarded native hooks for one exact ATS 1.62.1.1 x64 executable. It never patches the file on disk or writes `g_adaptive_shift` storage.

Every site is verified before transactional installation; failures roll back and shutdown restores patched instructions. Unknown executables or already modified sites are refused. Stubs stay allocated after shutdown, because another game thread may still be returning through one. An SDK reinitialization starts with fresh bookkeeping and stubs, and refuses installation while any earlier patch remains unrestored. The 20 sites consist of:

- Seven adaptive comparisons of the cruise/adaptive-block byte at `actor+0x204`; register-preserving stubs bypass the block only within supported active vehicle scope.
- Eleven mode-reader calls; while kickdown is active they return temporary High Power (`1.6667`) instead of the selected value. UI/serialization readers are untouched.
- Two `int(void *self)` gear-chooser calls at RVAs `0x9569A9` and `0x9569DB`, targeting `0x956FA0`; they may cap a repeated failed positive upshift. Stock downshifts, reverse, neutral and kickdown pass through.

The supported SHA-256 and individual RVAs live in `plugin/src/road_trip_shift.cpp`. A changed game build requires renewed investigation of all instructions and calling conventions, not just a new hash.

## Input and setting provenance

Vehicle scope accepts exactly the four Ford SDK IDs in `kickdown.h`, with or without one `vehicle.` prefix. Other identities fail closed. Keyboard W is polled for the foreground game only. Two distinct presses within 300 ms request kickdown while the second is held; holding a key is not a second press. Release, pause, focus loss and activity/vehicle changes cancel it; input held across such a change must be released before it counts as a press. W held in another window does not reset the cruise guard. Analog input uses 15% pressed and below 3% released; cruise-generated effective throttle is never treated as a physical pedal double tap.

Keyboard full-gas behavior comes from native Smart Throttle. The plugin does not synthesize full analog throttle. A single press retains the selected mode.

`CONFIG_MODE_INITIAL/CHANGED source=game_setting` observes the stored setting and does not attribute all changes to a person: profile loads can also change it. `KICKDOWN ON/OFF source=plugin` observes a separate temporary request. `mode_source` and `mode_override_changed` distinguish configured High Power from kickdown even when their values match. `mode_effective` is the value offered to hooks; `reader_calls`/`shadow_reads` measure actual use.

## Cruise protection in 0.3.0

`CruiseGuard` observes a higher-gear trial for at most 12 seconds. After speed loss of at least 0.6 m/s from trial entry, throttle at least 0.75 within 1.2 seconds, and an actual native downshift, it sets a temporary ceiling of `failed_gear - 1`. It does not force the first downshift. There is no fixed road speed or vehicle-specific gear target.

The minimum hold is 8 seconds; release requires 3 continuous seconds within 0.6 m/s of the cruise target at throttle no greater than 0.60. Physical acceleration, kickdown, braking, pauses, target/mode/vehicle changes and sample gaps reset learning. Effective brake telemetry covers ACC braking. The configured mode and throttle control stay untouched. Events use `source=cruise_load`, with `cruise_guard_cap` and `cruise_guard_clamps` counters. Set `anti_hunt=0` for comparison.

Traffic-following cruise may command a lower actual speed than its displayed setpoint. A speed drop alone does not establish insufficient engine power. Logging brake/input/load helps interpret traces, but does not expose every internal native decision.

## Validation and known limits

### Bounded diagnostics in 0.3.1

`RollingLog` shares a default 50 MiB quota across this plugin's timestamp/PID `.log` and `.csv` files. A session rotates both streams into parts of at most 5 MiB combined, including a CSV header in every part. The old 150,000-row stop is removed. Before opening a part, the old handles are closed, recognized completed files are scanned and the oldest are deleted until there is room for an entire new part. Thus the active part can grow without rescanning the directory on every frame. Older unsegmented sessions use the same quota. Name validation and reparse/directory exclusion protect unrelated files. New part names avoid truncation after reinitializing within the same second.

If deletion or a write fails, disk logging stops instead of exceeding the reserved budget. Existing locked oversized files cannot be shrunk, but no new part is opened while they prevent reservation. `status.json` is small and overwritten separately; game-owned logs and installer backups are outside the quota. `[logging] enabled=0` stops the plugin's diagnostic file/status writes and SDK log messages while mode observation, cruise logic and kickdown keep running. The default quota is applied even when updating an older INI without the new keys.

Retention tests cover quota bounds using actual file lengths during writes, event-only and mixed traffic, oldest legacy removal, independent CSV headers, latest samples, name collision avoidance, unrelated files and failed deletion/recovery. Native tests also check that disabling diagnostics leaves mode shadow/restoration functional and does not overwrite status.

Automated checks cover input timing/cancellation/restoration; all supported IDs and excluded vehicles; mode provenance; failed-upshift detection and release at several target speeds; manual/brake/kickdown bypass; all 20 current instruction sites; unsupported-host rejection; execution of the three comparison-stub variants; chooser bypasses; patch restoration, diagnostic disable and SDK/log lifecycle in isolated memory.

Base 0.2.2 behavior was exercised in real driving. The 0.3.0 detector recognized failed-upshift episodes in a local earlier trace, but replay is not a simulation of the corrected vehicle response. **The 0.3.0 cruise hold still needs a new driving test.** Hook initialization and passing harnesses are separate from confirming road behavior. No custom Crown Victoria calibration is applied; stock definitions alone do not establish real-world shift behavior.

The obsolete SII prototype globally reduced `rpm_range_power` and impaired acceleration/kickdown. It is not part of this distribution. Local traces, extracted game assets, disassembly and development toolchains are excluded from Git.

## References

- [SCS SDK](https://modding.scssoft.com/wiki/Documentation/Engine/SDK/Telemetry), version 1.16; headers and notice included.
- [car_automatic_transmission](https://github.com/deppelover/car_automatic_transmission), MIT, inspected at `254a2f6aba56b7e41b879143daec7ff40f3bac4f`; reference for call-site hooks.
- [DazDrive](https://github.com/DamienDutch/dazdrive), MIT; broader engineering reference. Neither upstream prebuilt DLL is included.

See [third-party notices](../third_party/README.md). Engine maps and vehicle definitions are not redistributed.
