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

### Validation and known issues

- DLL build, logic/native/log-retention tests, installed-game hook/HUD signature checks, package/checksums and public-file checks passed. Those checks do not establish correctness of every driving scenario. The remote CI workflow has not been run for this snapshot.
- The owner confirmed single-pedal Economy and initially successful keyboard kickdown on Bronco. The examined drive shows six successful kickdown activations before keyboard binding discovery fails.
- **Unfixed: keyboard kickdown can disappear as the game log grows.** Profile discovery rereads only the last 256 KiB and clears the selected path when the profile line leaves that tail. The same parser reproduces the difference between the full log prefix and its tail. Fix profile identity/lifecycle handling, with long-log and real profile-change regressions; do not mask it with a W fallback.
- **Open: High Power cruise and `anti_hunt` release.** The examined native chooser requests a higher gear while the plugin returns the learned ceiling. The initial downshift belongs to the game; subsequent ceiling enforcement belongs to the plugin. Review learning/release criteria, without fixed speed-to-gear rules. Do not use the owner's off-road/crash segment as validation.
- No previous sub-km/h Economy/Normal chatter appears in the examined drive: recovery entries exceed 3 km/h; exits follow target arrival or a selected-mode change. This is evidence from one drive, not a universal guarantee.
- The owner reports ACC recovery appears to work; broader confirmation remains pending. Per-press SDK confirmation, other vehicles and untested screen/ABI scenarios are not claimed complete.

### Review starting points

1. `keyboard_binding.h` and `refresh_keyboard_binding()` in `road_trip_shift.cpp`: profile discovery and invalidation, growing/truncated logs, stale binding risks and polling cost.
2. `cruise_guard.h`, `choose_gear()` and the adjacent/direct caller distinction: evidence for a failed upshift, ceiling lifetime, downshift preservation and mode/target changes.
3. `cruise_acceleration.h` and native reader integration: hysteresis, ACC/input cancellation, immediate High Power precedence and restoration.
4. `hook_memory.h`, hook installation/rollback/reinitialization and `native_hud.h`: executable-code lifetime, ABI and live-pointer assumptions.

The [implementation plan](IMPLEMENTATION_PLAN.md) owns current statuses and next checks. This changelog records what changed and what remains unresolved. Raw driving logs and private analysis artifacts are excluded from Git.
