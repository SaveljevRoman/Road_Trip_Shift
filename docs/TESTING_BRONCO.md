# Ford Bronco driving checks

[Русский](TESTING_BRONCO_RU.md) · [README](../README.md)

PR #1 test branch. Automated checks do not replace driving validation.

## Setup

Select the Ford Bronco, an automatic transmission and Economy. Compare the same road with consistent traffic/settings. First compare cruise without adaptive traffic following, then check ACC separately. Keep diagnostics enabled; an existing INI needs no changes. Release the accelerator and brake after loading.

Speeds below are reproduction examples, not plugin control thresholds or required gears. Record the case number and approximate time. Report the action, selected mode, actual/cruise speed and observed gears; logs can be read locally.

## Current manual scope

The owner deferred case 4 (pause/focus) and the controller check in case 7; these are not marked passed. Automated coverage remains. Compare held kickdown with repeated double presses after brief braking. The native `adjacent_limited` caller can limit downshifts after the plugin returns.

## Cases

1. **Single press:** Hold W in Economy, release and repeat after a pause. Expect the selected mode and no plugin `KICKDOWN ON`; a stock downshift alone does not establish plugin kickdown.
2. **Double press from Economy:** Double-tap W and hold the second press, repeating 3–5 times with different short release gaps within the configured interval. Fully release between attempts. Expect `KICKDOWN ON source=plugin`, `mode_config=10`, `effective_after=1.66670` and increasing `shadow_reads`. Release must log OFF and restore current Economy; the menu setting stays Economy.
3. **Rebinding:** Assign accelerator to S and brake to W. Resume, release both keys and repeat single/double acceleration with S. Expect identical behavior and `KEYBOARD_BINDING ... scans=001F`; W brakes without plugin kickdown. Restore assignments and repeat; W uses `scans=0011`.
4. **Pause/focus:** Pause during held kickdown, resume still holding, then release and perform a new double press. Repeat by changing foreground windows. Expect cancellation, no fresh press from continued holding, and normal double-press operation after release. The selected mode stays unchanged.
5. **High Power cruise:** Select High Power, accelerate and engage cruise. Reproduce approximately 90 and 105 mph (145 and 169 km/h) on the same road. After reaching the target, leave the accelerator alone for several minutes. Record repeated 9/10→5/6→9/10 cycles. A recognized failed upshift should produce `CRUISE_GUARD HOLD ... reason=native_load_reversal` and temporarily delay the next higher-gear attempt. Ordinary downshifts, braking and ACC remain available; one load-induced downshift is allowed. Suppression of repeated hunting still needs road validation.
6. **Economy speed recovery matches current plugin Normal:** Raise an active cruise target in Economy; expect `CRUISE_ACCEL ON`, `mode_effective=3`, unchanged `mode_config=10`, and OFF/Economy at the target. Then keep the target unchanged and lose speed on a grade/under load; the same Normal recovery must reactivate. Compare the same route with Normal selected in this plugin build. Manual acceleration stays Economy; kickdown has priority and braking cancels recovery. A reduced target needs Normal only while actual speed remains below it. Record time, target/actual speed and gears.
7. **Bypasses/controllers:** With a cruise ceiling active, press accelerator or brake; it should reset. If a controller/pedals are available, double-press only them without keyboard input; plugin kickdown must stay off. With High Power already selected, a keyboard double press still logs its separate request with `value_changed=0`.
8. **Memory:** After loading settles, drive 10–15 minutes, preferably repeated laps, with several pauses/resumes. Expect a `MEMORY` sample about every 10 seconds and stable hook allocation/byte counters during inputs and pauses. A separate SDK initialization, if one occurs, has a new `INIT` and may retain a counted published code page. Whole-process memory/handles include map loading and caches; growth is not automatically a DLL leak. Exit normally for the final memory sample.

## Additional ACC/cruise kickdown cases

- With Bronco/Economy/ACC, follow traffic below the unchanged target. ACC braking must retain authority. Once traffic clears and braking ends, expect existing Normal recovery (`reason=speed_deficit`), then Economy at target.
- During that recovery, double-tap the assigned keyboard accelerator and hold the second press. Expect plugin kickdown/High Power with the Economy setting unchanged. Release ends High Power; remaining cruise deficit resumes Normal, reaching the target restores Economy. Do not hold the manual brake at the same time.

## Focused kickdown-priority check

With Bronco automatic, repeat single/double keyboard gas in Off, Economy, Normal and High Power, first with cruise off and then during cruise recovery. Single gas keeps the selected manual mode; double/held gas must log plugin kickdown and expose High Power in every selected mode. While holding, change the selected game mode: effective High Power must persist until release. Release resumes the current selection or Normal recovery if Economy cruise remains below target. No particular gear or road speed is required. Manual pause/focus and controller cases remain deferred.

## Report

For each case: **number — pass/fail — observation — approximate time**. For cruise include selected mode, actual/target speed, gear sequence and ACC state. PR merge and release need separate owner confirmation after driving checks.
