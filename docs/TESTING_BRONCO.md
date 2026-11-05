# Selected-build Bronco driving checks

See the canonical [implementation plan](IMPLEMENTATION_PLAN.md) and [Russian checklist](TESTING_BRONCO_RU.md). Use a fresh diagnostic session and record each test time.

1. Single assigned keyboard gas retains Economy; double/hold requests temporary High Power; release restores the current mode/recovery policy. Repeat with a remapped key.
2. Retained main analog double-tap kickdown and analog_kickdown=0. No synthesized full analog gas.
3. Main pause/brake/focus cancellations, followed by a new double tap. The additional PR release-required latch is excluded.
4. Economy cruise target increases and subsequent speed loss: current plugin Normal enters beyond 3 km/h deficit, holds through the band and exits at target; small repeated crossings stay Economy. Inspect CRUISE_ACCEL precise error and transition intervals for boundary chatter.
5. Traffic/ACC braking and recovery after brake release; keyboard kickdown takes priority over Normal recovery.
6. High Power cruise under varying load, native candidate versus plugin return versus actual gear, anti_hunt comparison. Do not prescribe road speeds or numbered gears.
7. Native HUD colors, text, coalescing and duration; test this DLL separately from the previous visual confirmation.
8. MEMORY/PERF/ENGINE_STATE interpretation: process memory is distinct from thunk memory, DLL callback time is not frame time, engine telemetry does not prove limiter cuts.

Offline checks do not confirm driving behavior. Per-press SDK confirmation is deferred.
