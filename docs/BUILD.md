# Building Road Trip Shift

[README](../README.md) · [Русская версия](BUILD_RU.md)

## Requirements

- Windows x64 and PowerShell 7 (`pwsh`).
- MinGW-w64 `g++` with C++17. The pinned setup uses [w64devkit 2.11.0](https://github.com/skeeto/w64devkit/releases/tag/v2.11.0).
- SCS SDK 1.16 headers are included under `third_party/scs_sdk/include/`.
- A local ATS EXE is needed for full native-image verification. Game/DLC files are not distributed.

Run commands from the repository root. Replace `path_to_compiler`, `path_to_file` and `path_to_game` with your own locations.

## Compile and check

```powershell
pwsh -File ./plugin/build.ps1 -Compiler 'path_to_compiler/g++.exe' -GameExe 'path_to_game/bin/win_x64/amtrucks.exe'
```

Alternatively, download the pinned portable compiler. The setup verifies its archive SHA-256 and extracts into ignored `research/toolchain/`. Provide 7-Zip explicitly, or put `7z` on PATH:

```powershell
pwsh -File ./scripts/bootstrap-toolchain.ps1 -SevenZip 'path_to_file/7z.exe'
pwsh -File ./plugin/build.ps1 -GameExe 'path_to_game/bin/win_x64/amtrucks.exe'
```

The build finds the portable compiler or `g++` on PATH. It does not guess a game installation; pass `-GameExe` for full verification. To build without a local game:

```powershell
pwsh -File ./plugin/build.ps1 -NoGame
```

Without an EXE, logic/native harness checks still run, but the 20 game instruction sites are not verified. The manifest records this. Publication requires `-GameExe`.

## Outputs and publication

| Output | Contents |
| --- | --- |
| `.build/tests/` | Logic/native/log-retention test executables and local fixtures |
| `.build/package/` | Installation tree, installer, EN/RU guides, notices and manifest |
| `.build/road_trip_shift_0.3.1_win64.zip` | Installation archive; omitted with `-SkipPackage` |
| `release/` | Updated only with `-Publish`: DLL, ZIP, manifest and checksums |

Update the checked-in prebuilt distribution:

```powershell
pwsh -File ./plugin/build.ps1 -GameExe 'path_to_game/bin/win_x64/amtrucks.exe' -Publish
pwsh -File ./scripts/Check-PublicFiles.ps1
git diff --stat
```

`-Publish` requires native-image verification and cannot be combined with `-SkipPackage`. The DLL uses static compiler runtimes, stripped symbols, repository-prefix mapping and a fixed PE timestamp. The manifest contains hashes and validation status, without machine paths or user names. Building does not install the plugin or start/stop ATS.

## Verification

The build checks input/mode restoration, cruise-guard logic, SDK vehicle scope and provenance, instruction-pattern resolution, native condition/chooser execution, patch restoration and diagnostic disable in isolated memory, plus bounded log retention on a local file fixture. With `-GameExe`, it resolves all 20 instruction sites in that EXE and records its hash for reference. Synthetic images verify shifted code/data/functions, changed unrelated bytes and missing or ambiguous patterns. These checks do not prove driving behavior.

```powershell
pwsh -File ./scripts/Check-Instructions.ps1
pwsh -File ./scripts/Check-PublicFiles.ps1 -History
```

Optional local detector replay: `.build/tests/logic-tests.exe path_to_file/telemetry.csv`. This evaluates detection only, without simulating vehicle response to a different gear. Never commit telemetry or test output. Read [engineering notes](DEVELOPMENT.md) before changing hooks; the runtime resolves addresses automatically, and there is no version or game hash whitelist. Tested on ATS 1.62.1.1 (Windows x64).
