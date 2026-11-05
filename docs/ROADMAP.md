# Road Trip Shift roadmap

[Русский](ROADMAP_RU.md) · [README](../README.md)

## Current priority

Complete the [Bronco driving checks](TESTING_BRONCO.md) for PR #1: keyboard kickdown, cruise stability and process/plugin memory trends. Merge and release remain subject to the owner's separate confirmation. The feature below is planned research, not implemented or included in the current test DLL.

## Transmission-mode notification

Requested on 2026-11-05. Changing the adaptive transmission mode using an assigned game button is silent. Show a small, temporary indicator so the driver can see the selected mode without opening settings.

### Behavior

| Selected mode | Indicator color | Suggested text |
| --- | --- | --- |
| Off | Gray | Transmission: Off |
| Economy | Green | Transmission: Economy |
| Normal | Blue | Transmission: Normal |
| High Power | Red | Transmission: High Power |

- Start with colored text and optionally a small dot; a custom icon is optional. Rendering integration, rather than the choice of text versus icon, is the main engineering work.
- Show for 2–3 seconds after an observed game-setting change. Proposed default: 2.5 seconds, configurable via INI.
- Reuse the observed setting/provenance behind `CONFIG_MODE_CHANGED source=game_setting`. Follow the actual value, not a guessed mode order or a hardcoded mode-switch button. A mouse/menu change and a game-assigned key/controller button should produce the same result.
- Preserve the distinction between the user's selected mode and temporary plugin kickdown. Do not label a kickdown request as a saved-setting change.
- Rapid cycling updates one indicator and restarts its timer; never queue an unbounded list of notifications. Initial mode discovery is not itself a change. Unknown values must not be mislabeled Off.
- Use text as well as color, with a subtle background for legibility. EN/RU text, enable/disable, display duration, anchor and scale are planned settings; exact option names are not fixed yet.
- Position relative to the game client area, resolution and DPI; avoid fixed desktop coordinates. Show only while ATS is foreground and not minimized. Never steal focus or consume mouse/keyboard input.

### Research findings

Reviewed upstream documentation and project descriptions on 2026-11-05. References demonstrate approaches, not drop-in compatibility with Road Trip Shift.

| Reference | Relevant finding | Application |
| --- | --- | --- |
| [PrismTextureStreamer](https://github.com/Baldywaldy09/PrismTextureStreamer) | An ATS/ETS2 plugin loaded from the plugins directory, with an in-game Dear ImGui overlay and DirectX 11 integration | Reference for an embedded overlay. Its screen-texture capture/redirect machinery is unnecessary for a small mode notification. |
| [ETS2/ATS Telemetry Overlay](https://github.com/rpfilomeno/ETS2-ATS-Telemetry-Overlay) | Auto-hiding telemetry overlay; its README requires windowed mode | Reference for a window-based prototype. Do not assume that it covers exclusive fullscreen. |
| [ETS2 Local Radio](https://github.com/Koenvh1/ets2-local-radio) | An existing ETS2/ATS overlay with documented DirectX and resize-related behavior | Reference for expected integration and lifecycle cases. |
| [Dear ImGui Win32/DX11 example](https://github.com/ocornut/imgui/blob/master/examples/example_win32_directx11/main.cpp) | Upstream example covers renderer setup, frame drawing and device/window resource handling | Rendering reference; the example itself is a standalone application, not an ATS injection recipe. |
| [Microsoft extended window styles](https://learn.microsoft.com/en-us/windows/win32/winmsg/extended-window-styles) | Documents layered, no-activate and related window styles | Reference for a nonactivating window-based alternative; input pass-through and fullscreen behavior still require implementation/testing. |
| [Official SCS Telemetry SDK](https://modding.scssoft.com/wiki/Documentation/Engine/SDK/Telemetry) | Telemetry/input SDK; no documented notification-drawing API was found in the bundled 1.16 headers | Existing setting observation can supply events; a separate rendering mechanism is required unless further research finds an appropriate game UI entry point. |

### Implementation sequence

1. Finish the current Bronco/gearbox and memory investigation first. Keep the installed test DLL unchanged while the owner drives.
2. Prototype a tiny notification renderer separately from transmission logic. Compare an embedded DX11 draw path with a nonactivating window for the owner's actual display mode. Prefer minimal dependencies and no separate server/application requirement where practical; renderer selection remains open.
3. Connect it to actual setting-change observations, using session-resolved game addresses already available. Do not add fixed game addresses or a version whitelist. Rendering failure must leave gearbox operation intact.
4. Add a single bounded notification state, INI settings, EN/RU labels and diagnostics. A rendering thread must not call SDK APIs; transfer only the small notification state safely.
5. Verify windowed/borderless/fullscreen behavior before claiming support. Test rapid changes, repeated modes, initial load, unknown values, pause/focus loss, Alt-Tab, resize, DPI, device recreation, Steam overlays and diagnostics/notification disable.
6. Check CPU/frame-time impact and allocation/handle/resource counts through repeated notifications and device resets. No per-frame unbounded allocations, leaked textures/fonts/render targets or dangling hooks.
7. Review licensing before reusing upstream code. Update EN/RU installation and compatibility notes, and provide a separate test DLL after the current driving test is complete.

## Acceptance

The driver's mode-switch button produces the correct colored mode label for roughly 2–3 seconds, the notification disappears automatically, rapid changes show the latest confirmed mode, and it never changes transmission behavior or interrupts driving input. Display-mode support and memory behavior must be verified, not inferred from the reference projects.
