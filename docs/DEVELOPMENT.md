# Engineering notes

[README](../README.md) В· [Build](BUILD.md) В· [Installation](INSTALL.md)

## Scope and hooks

The official SCS SDK 1.16 supplies vehicle identity, input and telemetry, but has no documented command for replacing adaptive transmission decisions per shift. Native hooks are tested on ATS 1.62.1.1 x64. There is no version or executable hash whitelist, and the EXE on disk is unchanged.

`native_scan.h` resolves 20 hook sites from the 13 instruction signatures in `native_signatures.h`. Relative call, branch and RIP-relative data displacements are wildcards, allowing code, reader/chooser functions and configuration data to move independently. The reader and adaptive-mode variable are decoded from the matched instructions; no fixed game RVAs are compiled into the DLL.

Every site is verified before transactional installation; failures roll back and shutdown restores patched instructions. Missing/ambiguous instruction patterns or already modified sites are refused. Stubs stay allocated after shutdown, because another game thread may still be returning through one. An SDK reinitialization starts with fresh bookkeeping and stubs, and refuses installation while any earlier patch remains unrestored.

The sites consist of seven cruise/adaptive comparisons, eleven mode-reader calls and two automatic gear-chooser calls. All reader calls must agree on their target and configuration variable; both chooser calls must agree on their target. Only unique matches in readable executable image sections are used. Missing, ambiguous, overlapping or inconsistent matches leave hooks disabled, before any patch is applied. This protects against writing into unrelated memory without imposing a game-version whitelist.

Hook installation is transactional and shutdown restores patched instructions. Mode-reader hooks return temporary High Power during plugin kickdown, or Normal for selected Economy during the explicit increased-cruise-target acceleration phase; UI, serialization and stored `g_adaptive_shift` remain unchanged. Chooser hooks may cap a repeated failed positive upshift, while stock downshifts, reverse, neutral and kickdown pass through. Future changes to gearbox instructions or object layout can require plugin changes; signature relocation tests do not prove compatibility with untested game builds.

## Input and setting provenance

Vehicle scope accepts exactly the four Ford SDK IDs in `kickdown.h`, with or without one `vehicle.` prefix. Other identities fail closed. Keyboard accelerator bindings come from the active profile's `mix dforward` in `controls.sii`; no key is hardcoded. The latest canonical profile path in the current game-log tail selects a local or Steam profile. Documents redirection and `-homedir` are supported; an optional `input/controls_file` overrides discovery. The parser accepts supported keyboard keys and simple OR alternatives only, ignores controller-button alternatives and refuses ambiguous/unsupported bindings. Physical scan codes are translated using the foreground game thread's Windows keyboard layout before polling. Controller throttle by itself never qualifies.

Fresh SDK `truck.input.throttle` confirms that a held keyboard accelerator is producing game input; `truck.effective.throttle` is never a kickdown source. The first confirmed physical release preserves the armed first tap across any number of released frames until the configured double-tap timeout. An unconfirmed release clears arming only on its edge; repeated released frames must not erase a confirmed first tap. A keyboard release restores mode even while a controller still holds gas. Missing configuration disables only keyboard kickdown; cruise remains available.

The input channel requests `each_frame | no_value`. FRAME_START invalidates the previous sample; FRAME_END processes the fresh channel sample after all telemetry callbacks. An unavailable, invalid or missing sample cancels kickdown and blocks held input until release. Startup, pause/resume, focus loss, timer restart and vehicle/control changes do the same, including repeated cancellations without intervening frames. Two distinct physical keyboard presses within 300 ms request kickdown while the second is held. Count their physical edges before SDK confirmation: Smart Throttle starts with low/zero values and ramps up. Each stroke must produce positive fresh SDK input within the configured double-tap interval; no fixed throttle-percentage threshold or independent timeout is used. An unconfirmed released stroke cannot arm the next press; confirmation timeout or losing confirmed throttle blocks until release. Missing/invalid samples still cancel immediately. The former 0.15 threshold cancelled every initial ramp frame and blocked valid double taps. Unrelated keys and controller-only throttle cannot arm the detector. CSV `input_down` replaces `w_down`; `THROTTLE DOWN/UP source=game_input`, `origin=keyboard_accelerator_double` and `KEYBOARD_BINDING source=controls_sii ready=...` identify input without recording profile paths. The legacy INI `input/analog_kickdown` is ignored because analog kickdown is no longer supported.

Keyboard full-gas behavior comes from native Smart Throttle. The plugin does not synthesize full analog throttle. A single press retains the selected mode.

`CONFIG_MODE_INITIAL/CHANGED source=game_setting` observes the stored setting and does not attribute all changes to a person: profile loads can also change it. `KICKDOWN ON/OFF source=plugin` observes a separate temporary request. `mode_source` and `mode_override_changed` distinguish configured High Power from kickdown even when their values match. `mode_effective` is the value offered to hooks; `reader_calls`/`shadow_reads` measure actual use.

## Economy cruise acceleration

When an already active cruise target is increased with Economy selected, the plugin temporarily supplies Normal until speed reaches the new target. Target decrease, braking/ACC braking, manual gas, kickdown, pause or cruise cancellation restores the current selection. The setting is never written; Normal and High Power retain their own behavior. Later speed loss alone does not reactivate the exception. `CRUISE_ACCEL ON/OFF source=plugin`, `mode_source=cruise_acceleration` and CSV/status `cruise_acceleration_normal` distinguish this from a user change and kickdown. Disable using `[cruise] normal_acceleration=0`.

The first valid target observation establishes a baseline and does not activate the exception. Atomic `cruise_normal` is consumed by native readers, which check the original selected value before returning Normal. Kickdown has priority; guard learning is reset/bypassed while this acceleration phase is active. No new road-speed, RPM or gear thresholds are used. Automated checks cover several arbitrary target speeds and SDK state through the native mode reader. Driving validation is pending.

## Cruise protection in 0.3.0

`CruiseGuard` observes a higher-gear trial for at most 12 seconds. After speed loss of at least 0.6 m/s from trial entry, throttle at least 0.75 within 1.2 seconds, and an actual native downshift, it sets a temporary ceiling of `failed_gear - 1`. It does not force the first downshift. There is no fixed road speed or vehicle-specific gear target.

An additional detector follows the whole chain of upshifts instead of replacing its entry speed at each step. If the stock gearbox then downshifts while speed has fallen below both trial entry and the unchanged cruise target, and cruise demand has risen above its trial value, it learns the same ceiling (`reason=native_load_reversal`). This uses the native downshift and relative observations without a new fixed gas or speed threshold. It catches reversals below the earlier 0.75 throttle gate; braking, manual input, kickdown and target changes still reset learning.

The minimum hold is 8 seconds; release requires 3 continuous seconds within 0.6 m/s of the cruise target at throttle no greater than 0.60. Physical acceleration, kickdown, braking, pauses, target/mode/vehicle changes and sample gaps reset learning. Effective brake telemetry covers ACC braking. The configured mode and throttle control stay untouched. Events use `source=cruise_load`, with `cruise_guard_cap` and `cruise_guard_clamps` counters. Set `anti_hunt=0` for comparison.

The adjacent native caller clamps a chooser result to the current gear plus/minus one after our hook returns; the direct caller stores the result directly. The two existing hooks now capture stock candidates and plugin returns into atomic snapshots. `GEAR_CHOICE` is emitted on the SDK thread when a candidate changes, with at most a periodic 1.5-second repeat for unchanged active decisions. `GEAR_CHANGED` records observed gear transitions. This is diagnostic only: no extra hook sites, altered maps or forced gear targets are introduced. Candidates and final selected gears must not be conflated.

Traffic-following cruise may command a lower actual speed than its displayed setpoint. A speed drop alone does not establish insufficient engine power. Logging brake/input/load helps interpret traces, but does not expose every internal native decision.

## Validation and known limits

### Bounded diagnostics in 0.3.1

`RollingLog` shares a default 50 MiB quota across this plugin's timestamp/PID `.log` and `.csv` files. A session rotates both streams into parts of at most 5 MiB combined, including a CSV header in every part. The old 150,000-row stop is removed. Before opening a part, the old handles are closed, recognized completed files are scanned and the oldest are deleted until there is room for an entire new part. Thus the active part can grow without rescanning the directory on every frame. Older unsegmented sessions use the same quota. Name validation and reparse/directory exclusion protect unrelated files. New part names avoid truncation after reinitializing within the same second.

If deletion or a write fails, disk logging stops instead of exceeding the reserved budget. Existing locked oversized files cannot be shrunk, but no new part is opened while they prevent reservation. `status.json` is small and overwritten separately; game-owned logs and installer backups are outside the quota. `[logging] enabled=0` stops the plugin's diagnostic file/status writes and SDK log messages while mode observation, cruise logic and kickdown keep running. The default quota is applied even when updating an older INI without the new keys.

Retention tests cover quota bounds using actual file lengths during writes, event-only and mixed traffic, oldest legacy removal, independent CSV headers, latest samples, name collision avoidance, unrelated files and failed deletion/recovery. Native tests also check that disabling diagnostics leaves mode shadow/restoration functional and does not overwrite status.

Automated checks cover input timing/cancellation/restoration; all supported IDs and excluded vehicles; mode provenance; failed-upshift detection and release at several target speeds; manual/brake/kickdown bypass; all 20 current instruction sites; relocated signature resolution, changed unrelated bytes, missing/ambiguous patterns and inconsistent targets; execution of the three comparison-stub variants; chooser bypasses; patch restoration, diagnostic disable and SDK/log lifecycle in isolated memory.

Base 0.2.2 behavior was exercised in real driving. The 0.3.0 detector recognized failed-upshift episodes in a local earlier trace, but replay is not a simulation of the corrected vehicle response. **The SDK accelerator input and 0.3.0 cruise hold still need a new driving test.** Hook initialization and passing harnesses are separate from confirming road behavior. No custom Crown Victoria calibration is applied; stock definitions alone do not establish real-world shift behavior.

The obsolete SII prototype globally reduced `rpm_range_power` and impaired acceleration/kickdown. It is not part of this distribution. Local traces, extracted game assets, disassembly and development toolchains are excluded from Git.

## Hook memory and process diagnostics

`ThunkMemory` obtains the page size and allocation granularity from Windows once. Stub writes check the remaining allocation before writing. `PreparedThunks` releases unpublished pages on every preparation failure, including page protection and module-pin failures. Before the first possible patch write the allocation is marked published: rollback cannot prove that another thread has left a stub. Published pages remain committed through shutdown/reinitialization until process exit, with explicit retained-byte/block counters. Failed unpublished frees keep ownership and prevent silently replacing the pointer.

`MEMORY source=process` samples Windows private committed memory, current/peak working set, private-byte change since the first valid sample, and handle count. These belong to the **whole ATS process**, not exclusively the DLL. Separate `plugin_thunk_*` counters track allocated, freed, active and retained executable stub memory and free failures; these do not measure all CRT heap allocations. CSV/status expose the latest snapshot, its tick and validity. Sampling defaults to 10 seconds (`logging/memory_sample_ms`, configurable from 1000 to 60000 ms) and stops when logging is disabled. The existing log rotation budget still applies. Compare warmed-up repeated routes; asset loading and caches can grow the process without a plugin leak.

Tests exercise 64 failed preparations with zero remaining unpublished committed pages, exhausted stub-buffer rejection, intentional retention, Windows metric availability, sampling throttling and diagnostics disabled. The Bronco trace replay recognizes a native reversal; this is detector validation, not a simulation of changed vehicle response.

## References

- [SCS SDK](https://modding.scssoft.com/wiki/Documentation/Engine/SDK/Telemetry), version 1.16; headers and notice included.
- [car_automatic_transmission](https://github.com/deppelover/car_automatic_transmission), MIT, inspected at `254a2f6aba56b7e41b879143daec7ff40f3bac4f`; reference for call-site hooks.
- [DazDrive](https://github.com/DamienDutch/dazdrive), MIT; broader engineering reference. Neither upstream prebuilt DLL is included.

See [third-party notices](../third_party/README.md). Engine maps and vehicle definitions are not redistributed.
