# Engineering notes

[README](../README.md) В· [Build](BUILD.md) В· [Installation](INSTALL.md)

## Permanent control constraint

Never force a numbered gear at a fixed road speed. Do not create speed-to-gear tables, fixed kickdown/cruise gear targets or thresholds copied from owner examples, screenshots or traces. Such numbers are diagnostic examples only. Use the native gearbox candidate and current game data; any experimental failed-upshift ceiling must be learned from an observed failed shift, rather than a fixed gear target. This project decision is recorded in [MEMORY.md](../MEMORY.md) and the synchronized contributor instructions.

## Scope and hooks

The official SCS SDK 1.16 supplies vehicle identity, input and telemetry, but has no documented command for replacing adaptive transmission decisions per shift. Native hooks are tested on ATS 1.62.1.1 x64. There is no version or executable hash whitelist, and the EXE on disk is unchanged.

`native_scan.h` resolves 20 hook sites from the 13 instruction signatures in `native_signatures.h`. Relative call, branch and RIP-relative data displacements are wildcards, allowing code, reader/chooser functions and configuration data to move independently. The reader and adaptive-mode variable are decoded from the matched instructions; no fixed game RVAs are compiled into the DLL.

Every site is verified before transactional installation; failures roll back and shutdown restores patched instructions. Missing/ambiguous instruction patterns or already modified sites are refused. Stubs stay allocated after shutdown, because another game thread may still be returning through one. An SDK reinitialization starts with fresh bookkeeping and stubs, and refuses installation while any earlier patch remains unrestored.

The sites consist of seven cruise/adaptive comparisons, eleven mode-reader calls and two automatic gear-chooser calls. All reader calls must agree on their target and configuration variable; both chooser calls must agree on their target. Only unique matches in readable executable image sections are used. Missing, ambiguous, overlapping or inconsistent matches leave hooks disabled, before any patch is applied. This protects against writing into unrelated memory without imposing a game-version whitelist.

Hook installation is transactional and shutdown restores patched instructions. Mode-reader hooks return temporary High Power during plugin kickdown, or Normal for selected Economy during active cruise speed recovery; UI, serialization and stored `g_adaptive_shift` remain unchanged. Chooser hooks may cap a repeated failed positive upshift, while stock downshifts, reverse, neutral and kickdown pass through. Future changes to gearbox instructions or object layout can require plugin changes; signature relocation tests do not prove compatibility with untested game builds.

## Input and setting provenance

Vehicle scope accepts exactly the four Ford SDK IDs in `kickdown.h`, with or without one `vehicle.` prefix. Other identities fail closed. Keyboard accelerator bindings come from the active profile's `mix dforward` in `controls.sii`; no key is hardcoded. The latest canonical profile path in the current game-log tail selects a local or Steam profile. Documents redirection and `-homedir` are supported; an optional `input/controls_file` overrides discovery. The parser accepts supported keyboard keys and simple OR alternatives only, ignores controller-button alternatives and refuses ambiguous/unsupported bindings. Physical scan codes are translated using the foreground game thread's Windows keyboard layout before polling. Controller throttle does not qualify as keyboard input; the separate main analog detector remains enabled by default.

Keyboard kickdown follows the active profile accelerator binding in controls.sii, with no hardcoded W fallback. Double-tap, hold, release and pause/brake cancellation use the main branch policy. Fresh SDK throttle is diagnostic and does not confirm or gate keyboard presses. Per-press SDK confirmation and Smart Throttle ramp handling are deferred in the [implementation plan](IMPLEMENTATION_PLAN.md). Main analog kickdown is retained: double-pressing a pedal/trigger temporarily requests High Power; input/analog_kickdown=0 disables it. The plugin does not synthesize full analog throttle. The default double-tap interval is 300 ms.

The SDK input channel still requests each_frame | no_value for diagnostics and cruise recovery validity. FRAME_START invalidates the sample; FRAME_END processes input after channel callbacks. Missing SDK input cancels Economy cruise recovery, but does not gate physical keyboard kickdown. Analog detection retains main thresholds (15% pressed, below 3% released), with keyboard activity suppressing duplicate analog detection for 250 ms. The extra PR release-required latch is excluded.

Keyboard full-gas behavior comes from native Smart Throttle. The plugin does not synthesize full analog throttle. A single press retains the selected mode.

`CONFIG_MODE_INITIAL/CHANGED source=game_setting` observes the stored setting and does not attribute all changes to a person: profile loads can also change it. `KICKDOWN ON/OFF source=plugin` observes a separate temporary request. `mode_source` and `mode_override_changed` distinguish configured High Power from kickdown even when their values match. `mode_effective` is the value offered to hooks; `reader_calls`/`shadow_reads` measure actual use.

## Keyboard kickdown precedence

A double press of the assigned keyboard accelerator has the highest transmission-mode priority: temporary High Power overrides Off, Economy, Normal, already selected High Power, custom values, cruise holding/recovery and both cruise options. Entering kickdown immediately clears pending Normal recovery; recovery cannot re-enable Normal while kickdown is held. Changing the selected game mode during the hold does not interrupt High Power. Release resumes the current selection/recovery policy, without saving or overwriting the game setting. As in main, full-throttle acceleration comes from the game's Smart Throttle and native gearbox maps; the DLL does not synthesize throttle or assign a numbered gear. Input validity, supported-vehicle scope, plugin enable and cancellation checks still apply.

The native-input regression matrix covers five selected values, both cruise options on/off and logging on/off (40 contexts), selection changes during the held second press and unchanged native gear candidates. A separate local comparison against the actual main header confirms identical active-kickdown mode results with recovery on/off. This validates mode selection, not vehicle dynamics.

## Economy cruise acceleration

With Economy selected, cruise starts temporary Normal recovery only when speed is more than 3 km/h below its current target. Once active, recovery continues through the 3 km/h band and returns to Economy at the target; it does not wait for a 3 km/h overshoot. This target-relative hysteresis applies to target increases, later speed loss and recovery after ACC braking. Manual gas, kickdown, braking, unavailable input, pause or cruise cancellation cancels recovery; after cancellation the entry band applies again. Small target changes inside the band do not repeatedly activate Normal. Lowering the target stops recovery when that target is reached. The current plugin Normal hooks and cruise guard remain active, High Power kickdown has priority, and the saved game setting is never written. Diagnostics use `CRUISE_ACCEL source=plugin`, `mode_source=cruise_acceleration` and `cruise_acceleration_normal`. Disable using `[cruise] normal_acceleration=0`.

The first valid cruise observation can activate recovery if speed is already more than 3 km/h below target. Atomic `cruise_normal` is consumed by native readers, which check the original selected value before returning Normal. Kickdown has priority. The same chooser and guard as the current plugin Normal mode remain active; temporary Normal does not reset or bypass their learning. The 3 km/h band is relative to the current target, with no fixed road-speed-to-gear, RPM or numbered-gear rule. Automated checks cover several arbitrary target speeds and SDK state through the native mode reader. Driving validation is pending.

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

Base 0.2.2 behavior was exercised in real driving. The 0.3.0 detector recognized failed-upshift episodes in a local earlier trace, but replay is not a simulation of the corrected vehicle response. **The selected keyboard/analog input and cruise recovery/hold still need a new driving test.** Hook initialization and passing harnesses are separate from confirming road behavior. No custom Crown Victoria calibration is applied; stock definitions alone do not establish real-world shift behavior.

The obsolete SII prototype globally reduced `rpm_range_power` and impaired acceleration/kickdown. It is not part of this distribution. Local traces, extracted game assets, disassembly and development toolchains are excluded from Git.

## Hook memory and process diagnostics

`ThunkMemory` obtains the page size and allocation granularity from Windows once. Stub writes check the remaining allocation before writing. `PreparedThunks` releases unpublished pages on every preparation failure, including page protection and module-pin failures. Before the first possible patch write the allocation is marked published: rollback cannot prove that another thread has left a stub. Published pages remain committed through shutdown/reinitialization until process exit, with explicit retained-byte/block counters. Failed unpublished frees keep ownership and prevent silently replacing the pointer.

`MEMORY source=process` samples Windows private committed memory, current/peak working set, private-byte change since the first valid sample, and handle count. These belong to the **whole ATS process**, not exclusively the DLL. Separate `plugin_thunk_*` counters track allocated, freed, active and retained executable stub memory and free failures; these do not measure all CRT heap allocations. CSV/status expose the latest snapshot, its tick and validity. Sampling defaults to 10 seconds (`logging/memory_sample_ms`, configurable from 1000 to 60000 ms) and stops when logging is disabled. The existing log rotation budget still applies. Compare warmed-up repeated routes; asset loading and caches can grow the process without a plugin leak.

Tests exercise 64 failed preparations with zero remaining unpublished committed pages, exhausted stub-buffer rejection, intentional retention, Windows metric availability, sampling throttling and diagnostics disabled. The Bronco trace replay recognizes a native reversal; this is detector validation, not a simulation of changed vehicle response.

## Optional native HUD and engine diagnostics

The optional native HUD path in `native_hud.h` resolves three independent instruction signatures: a keyed-notification wrapper, key-specific delayed expiry, and a live gameplay/HUD pointer chain. It cross-checks the expiry call target and the keyed wrapper's enqueue ABI. RIP-relative data/function addresses, pointer-chain offsets and native fade duration are decoded at startup; no extra process patches are installed. Missing/ambiguous/inconsistent sites disable HUD independently of gearbox hooks. Live pointers are re-read on the SDK frame-end callback, never cached across world changes. Readability checks and main-thread ownership reduce invalid-pointer risk; they cannot establish compatibility with every future private ABI.

`mode_notice.h` observes only selected-mode changes, suppresses first discovery, coalesces pending changes and bounds each notification's lifetime. Frame-end emits one keyed, silent, lowest-priority persistent announcement with native PML text, then requests native expiry early enough to include the decoded fade delay in the configured lifetime. Text is copied into game-owned strings; secondary/icon strings point into game-owned constant storage, and no plugin callback is retained. Shutdown/pause/focus/vehicle transitions expire only the plugin key, without dereferencing a cached dead-world manager. Placement and visibility remain controlled by ATS. `MODE_NOTICE REQUEST` is not a render acknowledgement. See [behavior and manual checks](MODE_NOTICE.md).

Native tests execute the real keyed wrapper bytes against a recording callee for 1000 requests and execute key-only expiry against synthetic arrays. Relocated/missing/duplicate/inconsistent patterns, runtime timers, menu deferral, disabled HUD/logging, unknown labels and empty world pointers are checked. The installed game image is mapped as data for signature verification; its functions are not executed by offline tests. Actual rendering, color interpretation, fullscreen/windowed behavior, coexistence with other HUD notices and long-run game-owned allocation behavior require driving validation.

Optional engine diagnostics add CSV columns `engine_enabled`, `motor_brake`, `parking_brake`, `electric_enabled`, `retarder_level`, `input_clutch`, `effective_clutch`, `engine_wear` and `engine_signals_valid`. Values of -1 mean unavailable, never a confirmed off/zero state. Validity-mask bits 0–7 follow that order. `ENGINE_STATE source=game_telemetry` records initial availability, digital/retarder changes and clutch zero/nonzero edges immediately in SDK callbacks, with speed/RPM/gear/throttle/brake context; CSV samples continuous clutch/wear values. Channel registration is optional and failures do not disable the transmission. Vehicle changes reset validity. The SDK has no direct fuel-cut/rev-limiter flag: these signals can distinguish engine enable/braking/clutch events, but cannot by themselves prove a brief limiter cut.

The keyed wrapper writes a temporary optional key into its ninth incoming stack slot. The Win64 declaration reserves that slot explicitly; the expanded runtime harness exposed corruption when it was omitted, and now covers the corrected call layout. No new DLL was installed during that failed harness run.

## References

The [SCS SDK reference map](SDK_REFERENCES.md) links the official download/documentation and the exact bundled header comments used for lifecycle, channel availability, units and frame ordering. It separates those documented interfaces from native gearbox/HUD research. See the [changelog](CHANGELOG.md) for the review snapshot and known unfixed issues.

`performance.h` uses QueryPerformanceCounter/Frequency; raw ticks are aggregated and converted only for reports. `PerfScope` records early returns too. Atomic relaxed cumulative counts/totals/maxima/latency buckets avoid file I/O and allocations in native hooks. Clock origin/frequency and counters survive SDK reinitialization; snapshots of separate atomics are approximate while another thread is updating them. Read-mode sampling reuses its existing call sequence; chooser sampling observes its call counter. Default sampling is every 64 calls. There is no per-hook event logging. Clock output in nanoseconds does not increase QPC resolution, and a zero-duration short function can be below one clock tick. Totals contain nested timing/aggregation costs and are not additive exclusive CPU times. Event logging includes formatting, file flush and the SDK log sink; CSV/status profiling includes formatting and file operations. These measurements require an in-game trace to assess the owner's performance hypothesis.

- [SCS SDK](https://modding.scssoft.com/wiki/Documentation/Engine/SDK/Telemetry), version 1.16; headers and notice included.
- [car_automatic_transmission](https://github.com/deppelover/car_automatic_transmission), MIT, inspected at `254a2f6aba56b7e41b879143daec7ff40f3bac4f`; reference for call-site hooks.
- [DazDrive](https://github.com/DamienDutch/dazdrive), MIT; broader engineering reference. Neither upstream prebuilt DLL is included.

See [third-party notices](../third_party/README.md). Engine maps and vehicle definitions are not redistributed.

Current feature selection and deferred work: [implementation plan](IMPLEMENTATION_PLAN.md).

CRUISE_ACCEL transition diagnostics include six-decimal target/speed/error, transition count and interval, input/effective throttle, both brakes, SDK availability, keyboard/kickdown, scope/focus, normal option, guard cap, gear and RPM. CSV target/speed also use six decimals. Recovery now enters beyond a 3 km/h deficit and exits at the target; repeated small target crossings remain Economy. See the implementation plan.
