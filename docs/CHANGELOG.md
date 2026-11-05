# Changelog

[Русская версия](CHANGELOG_RU.md) · [Implementation plan](IMPLEMENTATION_PLAN.md) · [SCS SDK references](SDK_REFERENCES.md)

## Unreleased review snapshot — 2026-11-05

Base: `main` at `93e3c34`. Selected changes were transferred from PR #1 (`d3c4087` and its uncommitted HUD/diagnostic work) into `fix/selected-features-from-main` in the primary checkout. The DLL still identifies as **0.3.1 experimental**; this is a local review/test snapshot, not a new public release.

### Added and changed

- Discover the keyboard accelerator binding from the active profile's `controls.sii`, including supported remappings, local/Steam profiles, redirected Documents and `-homedir`; no hardcoded W fallback.
- Preserve the main double-tap/hold/release detector, analog pedal/trigger kickdown and pause/brake cancellation policy. Per-press SDK confirmation and the extra PR release-required latch are excluded; SDK confirmation remains deferred.
- Economy cruise temporarily uses the current plugin Normal path to recover speed. Recovery starts beyond a **3 km/h deficit**, stays active inside the band and returns to Economy at the target. ACC braking and driver input cancel recovery; re-entry uses the same band. Temporary High Power has priority and does not overwrite the saved setting.
- Preserve the main `anti_hunt` foundation, add native-downshift/increased-demand evidence and account for the current gear so a ceiling does not itself create or deepen a downshift.
- Fix hook reinitialization bookkeeping and reject reinstallation over unrestored patches. Free unpublished executable blocks on preparation failures; retain published blocks until process exit with separate accounting.
- Add a keyed native ATS HUD notice for selected transmission-mode changes: EN/RU, mode colors, approximately 2.5 seconds, rapid-change coalescing. Correct the Win64 native-call argument layout exposed by the offline HUD harness.
- Extend event/CSV/status diagnostics with input gates, configured/temporary mode provenance, native gear candidates, plugin returns and observed gears. Add engine/electrical/brake/retarder/clutch/wear availability, process memory/handles, hook-memory counters and sampled QPC timings. Cruise transitions record precise error, interval and entry band.
- Retain automatic discovery of 20 hook sites without a version/hash whitelist, stock engine/shift maps, bounded 50 MiB logs and optional diagnostic disable.
- Expand logic/native tests, HUD ABI/signature checks and GitHub Actions compilation/offline tests. Update EN/RU installation and driving checklists.
- Add the canonical implementation plan and persistent project decisions. Archive the old dirty worktree and local recovery material outside the repository before removing obsolete local build/prototype/research copies; retain the compiler and IDE settings.
- Add root `prompt.md` with a self-contained Claude review handoff, selected behavior, known unfixed issues, source entry points and verification commands.

### Review fixes

- **Keyboard binding no longer depends on a 256 KiB log tail.** `ProfileLog` follows `game.log.txt` incrementally: it reads only appended bytes, keeps the newest profile path of the whole current log and clears it only for a new or truncated log (smaller size or different first 64 bytes). The once-per-second 256 KiB reread on the game thread is gone. Tests cover a 1 MiB log whose old tail loses the profile, paths split between reads, a real profile switch and a restarted log; the native fixture grows the log past 256 KiB.
- **Why the binding was lost after about five minutes:** every plugin event (PERF, GEAR_CHOICE, MEMORY, ENGINE_STATE, INPUT_GATE and others) was also echoed to the SDK log, which is `game.log.txt`. By estimate that is about 1 KB/s, enough to push the profile line out of a 256 KiB tail in roughly five minutes. Driving diagnostics now stay in the plugin's own logs; only lifecycle/configuration messages reach the game log.
- **`-homedir`:** the game keeps its data in `American Truck Simulator` inside that path (community reports, not checked on the owner's setup). The path was previously used directly, so the game log could not be found there.
- **`anti_hunt` release under ACC:** release required speed within 0.6 m/s of the displayed cruise target. Adaptive cruise behind slower traffic can stay below it without braking, so a learned ceiling could hold indefinitely. Release now needs the 8 s minimum hold plus 3 s of eased load: throttle at most 0.60 while speed does not fall by more than 0.6 m/s. Downhill overspeed without throttle also releases it.
- **`native_load_reversal` noise:** any throttle increase above the trial value counted as extra demand. It must now exceed that value by the guard's existing 0.05 noise floor, already used for pedal and brake. The recorded Bronco chain still learns its ceiling.
- Checks: logic tests pass (macOS clang and MinGW build); the DLL, native and logging tests compile with MinGW GCC 16 and the project's `-Werror` flags. Native and logging tests were **not executed** (no Windows host), and the package and installed-game signature checks were not rebuilt. The trip log was unavailable, so it is not established that the `anti_hunt` changes explain the High Power episode.

### Windows package validation — 2026-11-05

- Built Claude's review changes at `a6998cc` on Windows through `plugin/build.ps1 -GameExe path_to_game/bin/win_x64/amtrucks.exe -Publish`, without changing implementation code.
- Logic, native and log-retention tests passed, including growing-log profile discovery, diagnostic routing, native HUD execution and all 20 gearbox/HUD signature checks against the installed ATS image. DLL, ZIP, build manifest and SHA-256 sums in `release/` were refreshed.
- Installed the same DLL locally with a backup of the previous one; the existing INI was verified unchanged. The version remains 0.3.1 experimental. These changes still need driving validation; no public GitHub release was created.

### Validation and known issues

- DLL build, logic/native/log-retention tests, installed-game hook/HUD signature checks, package/checksums and public-file checks passed. Those checks do not establish correctness of every driving scenario. The remote CI workflow has not been run for this snapshot.
- The owner confirmed single-pedal Economy and initially successful keyboard kickdown on Bronco. The examined drive shows six successful kickdown activations before keyboard binding discovery fails.
- **Fixed in code, driving check pending: keyboard kickdown could disappear as the game log grew.** Profile discovery reread only the last 256 KiB and cleared the selected path when the profile line left that tail; see Review fixes. No W fallback was added.
- **Open: High Power cruise and `anti_hunt` release.** The examined native chooser requests a higher gear while the plugin returns the learned ceiling. The initial downshift belongs to the game; subsequent ceiling enforcement belongs to the plugin. Learning noise and ACC-independent release were changed (see Review fixes); compare with `anti_hunt=0` on the same road before considering it resolved. No fixed speed-to-gear rules. Do not use the owner's off-road/crash segment as validation.
- No previous sub-km/h Economy/Normal chatter appears in the examined drive: recovery entries exceed 3 km/h; exits follow target arrival or a selected-mode change. This is evidence from one drive, not a universal guarantee.
- The owner reports ACC recovery appears to work; broader confirmation remains pending. Per-press SDK confirmation, other vehicles and untested screen/ABI scenarios are not claimed complete.

### Review starting points

1. `keyboard_binding.h` and `refresh_keyboard_binding()` in `road_trip_shift.cpp`: profile discovery and invalidation, growing/truncated logs, stale binding risks and polling cost.
2. `cruise_guard.h`, `choose_gear()` and the adjacent/direct caller distinction: evidence for a failed upshift, ceiling lifetime, downshift preservation and mode/target changes.
3. `cruise_acceleration.h` and native reader integration: hysteresis, ACC/input cancellation, immediate High Power precedence and restoration.
4. `hook_memory.h`, hook installation/rollback/reinitialization and `native_hud.h`: executable-code lifetime, ABI and live-pointer assumptions.

The [implementation plan](IMPLEMENTATION_PLAN.md) owns current statuses and next checks. This changelog records what changed and what remains unresolved. Raw driving logs and private analysis artifacts are excluded from Git.
