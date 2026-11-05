# Native transmission-mode notification

[Русский](MODE_NOTICE_RU.md)

The PR test build requests a small notification through ATS's own HUD. No separate window, graphics hook, ImGui, OBS or companion process is used. The first observation of the selected transmission mode is silent. A later actual setting change requests grey **Off**, green **Eco**, blue **Normal** or red **High Power** text. Unknown settings say **Custom**. Temporary kickdown and cruise recovery do not create setting-change notifications.

Rapid cycling replaces one keyed request and restarts its timer. Default lifetime is approximately 2.5 seconds including the game's native expiry delay. A change made in the settings menu is deferred until driving resumes. An existing notification expires on pause/focus loss, vehicle change or shutdown. Only supported vehicles and an enabled plugin request notices. HUD placement, scale, animation and visibility follow the game's own notification system and user settings; this version does not supply independent positioning or scaling. Other higher-priority game notifications can delay or suppress the notice.

Existing INI files need no edits. Optional settings in `road_trip_shift/road_trip_shift.ini`:

```ini
[hud]
enabled=1
duration_ms=2500
language=auto
```

`duration_ms` accepts 1000–10000 ms. `language=auto` uses the Windows UI language (Russian for Russian Windows, English otherwise); `ru` and `en` select an explicit language. This is independent of the game language. `logging/enabled=0` does not disable notifications.

`MODE_NOTICE READY source=game_hud` means the optional native signatures resolved. `REQUEST` and `EXPIRE_REQUEST` record requests, not proof of rendered pixels. Missing, ambiguous or inconsistent HUD signatures disable only the indicator. No game-version/hash whitelist or fixed game addresses are used; the executable on disk is not changed.

Offline checks resolve the keyed show/expiry/root paths against the installed ATS 1.62.1.1 executable and execute the actual native wrapper bytes against a recording test callee. They cover relocated code/data, missing/ambiguous/inconsistent signatures, 1000 calls, key-specific expiry, initial silence, rapid changes, pause/menu deferral, timeout and diagnostics disabled. **Actual in-game appearance, colors, lifetime and display-mode behavior still require testing.** No extra graphics-resource allocations are introduced by the DLL; game-owned notification/string allocations are not included in the thunk counters.

On Bronco: cycle all four modes, verify labels/colors and automatic disappearance; cycle quickly and ensure only the latest label remains. Repeat through the settings menu, then trigger keyboard kickdown without changing the setting: it must not produce a false selected-mode notice. Check the existing game HUD notification settings if nothing appears. Note the approximate time for log matching.

The native implementation was independently derived from the installed executable. SCS documents [PML in its game-radio UI](https://modding.scssoft.com/wiki/Documentation/Engine/Game_Radio); this does not document or guarantee the private HUD-call ABI. No third-party framework source is included.
