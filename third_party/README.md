# Third-party sources

- `scs_sdk/include/`: official SCS Telemetry & Input SDK 1.16, downloaded from https://download.eurotrucksimulator2.com/scs_sdk_1_16.zip on 2026-11-04. License: `licenses/SCS-SDK.txt`.
- `licenses/Deppelover-MIT.txt`: car_automatic_transmission, inspected at commit `254a2f6aba56b7e41b879143daec7ff40f3bac4f`. Source: https://github.com/deppelover/car_automatic_transmission . Reference for call-site hooks; the upstream DLL is not included.
- `licenses/DazDrive-MIT.txt`: DazDrive, inspected as an engineering reference. Source: https://github.com/DamienDutch/dazdrive . The upstream DLL and source checkout are not included.
- `licenses/MinGW-w64-runtime.txt`: runtime notices supplied with the pinned w64devkit toolchain; retained for the statically linked runtime portions.
- `licenses/GPL-3.0.txt` and `licenses/GCC-Runtime-Exception.txt`: GCC runtime/header terms (GPLv3 with Runtime Library Exception 3.1), from the [GCC source tree](https://github.com/gcc-mirror/gcc). See the [official runtime license explanation](https://gcc.gnu.org/onlinedocs/libstdc++/manual/license.html). These notices do not designate the project's own code as GPL.

The compiler and downloaded source trees are local dependencies excluded from Git. `scripts/bootstrap-toolchain.ps1` pins w64devkit 2.11.0 and verifies its archive hash. Game binaries, DLC archives, extracted engine definitions and disassembly are excluded; native tests map the locally supplied executable as a resource without starting the game.

Distribution ZIPs retain these notices in `licenses/`. The upstream plugin binaries are not distributed. Project source and the prebuilt DLL belong to this repository's author; the third-party licenses apply to their respective material and do not by themselves license the whole project.
