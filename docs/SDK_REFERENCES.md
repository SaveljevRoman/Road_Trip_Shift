# SCS SDK documentation and implementation references

[Engineering notes](DEVELOPMENT.md) · [Third-party provenance](../third_party/README.md)

## Official SCS sources

- [SCS Modding Wiki: Telemetry & Input SDK](https://modding.scssoft.com/wiki/Documentation/Engine/SDK/Telemetry): official SDK overview and download entry point. The wiki states that API documentation and examples are inside the archive.
- [Official SCS SDK 1.16 archive](https://download.eurotrucksimulator2.com/scs_sdk_1_16.zip): the distribution from which this repository's headers were taken. The SDK package version is 1.16; the bundled telemetry header separately defines its current telemetry API version as 1.01. Do not treat those version numbers as interchangeable.
- [SCS SDK license bundled with the project](../third_party/licenses/SCS-SDK.txt). Download provenance is recorded in [third_party/README.md](../third_party/README.md).

The comments and declarations in the bundled SCS headers are the precise API reference used by this implementation. They remain available offline and identify the version under review:

| Subject | Bundled official source | Use in Road Trip Shift |
| --- | --- | --- |
| Initialization, shutdown and registration interface | [scssdk_telemetry.h](../third_party/scs_sdk/include/scssdk_telemetry.h) | Plugin lifecycle and callback registration |
| Frame events, pause/start, configuration and timer restart | [scssdk_telemetry_event.h](../third_party/scs_sdk/include/scssdk_telemetry_event.h) | Frame start precedes channel delivery; frame end follows it |
| Channel callback semantics and registration flags | [scssdk_telemetry_channel.h](../third_party/scs_sdk/include/scssdk_telemetry_channel.h) | each_frame/no_value handling, NULL for unavailable values and unspecified ordering between channel callbacks |
| Vehicle channels, units and types | [scssdk_telemetry_truck_common_channels.h](../third_party/scs_sdk/include/common/scssdk_telemetry_truck_common_channels.h) | Physical/effective throttle and brake, speed/cruise, RPM/gear, engine/electrical state, retarder, clutch and wear |
| Configuration records | [scssdk_telemetry_common_configs.h](../third_party/scs_sdk/include/common/scssdk_telemetry_common_configs.h) | Vehicle configuration identity and changes |
| ATS-specific telemetry declarations | [scssdk_telemetry_ats.h](../third_party/scs_sdk/include/amtrucks/scssdk_telemetry_ats.h) | Game-specific supported telemetry surface |
| Value representation | [scssdk_value.h](../third_party/scs_sdk/include/scssdk_value.h) | Type checks and extraction of float, signed/unsigned and boolean values |
| Input-device API | [scssdk_input.h](../third_party/scs_sdk/include/scssdk_input.h) | Scope of the documented input-device interface; the plugin does not use it to synthesize full throttle |

## Boundary between SDK facts and native research

The documented SDK supplies telemetry and lifecycle/input-device interfaces. This project uses its headers to read game state; keyboard bindings in `controls.sii` and profile paths in the game log are separately parsed game-file formats.

Adaptive-mode reader hooks, native gear-chooser call sites, instruction patterns, keyed HUD calls and their private Win64 ABI are **project research, not documented SCS SDK contracts**. Their provenance and limits are described in [engineering notes](DEVELOPMENT.md), [HUD notes](MODE_NOTICE.md) and the third-party references. Signature/ABI tests against one game image do not turn those private interfaces into supported SDK APIs or prove compatibility with every game version.

The public header set used here does not expose a direct rev-limiter/fuel-cut flag. Engine/brake/clutch diagnostics help interpret observations and cannot establish a limiter event by themselves. Likewise, a native gear candidate, the plugin return value and the observed engaged gear are different stages, not interchangeable telemetry facts.
