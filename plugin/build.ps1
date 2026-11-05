param(
    [string]$Compiler='',
    [string]$GameExe='',
    [switch]$NoGame,
    [switch]$SkipPackage,
    [switch]$Publish
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
& (Join-Path $root 'scripts/Check-Instructions.ps1')
if($NoGame -and $GameExe){throw 'Choose -NoGame or -GameExe, not both.'}
if($Publish -and (-not $GameExe -or $NoGame -or $SkipPackage)) {
    throw '-Publish requires -GameExe and a complete installation ZIP.'
}
if(-not $Compiler) {
    $localCompiler=Join-Path $root 'research/toolchain/w64devkit/bin/g++.exe'
    if(Test-Path -LiteralPath $localCompiler){$Compiler=$localCompiler}
    elseif(Get-Command g++ -ErrorAction SilentlyContinue){$Compiler=(Get-Command g++).Source}
    else {throw 'Provide -Compiler path_to_compiler/g++.exe or run scripts/bootstrap-toolchain.ps1.'}
}
if(-not (Test-Path -LiteralPath $Compiler)){throw 'Compiler not found.'}
if($GameExe -and -not (Test-Path -LiteralPath $GameExe)){throw 'GameExe not found.'}
$source=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'src/road_trip_shift.cpp') -Raw
$version=[regex]::Match($source,'VERSION="([0-9.]+) experimental"').Groups[1].Value
if(-not $version){throw 'Cannot read source version.'}
$testedHash=if($GameExe){(Get-FileHash -LiteralPath $GameExe).Hash}else{$null}
$sdk=Join-Path $root 'third_party/scs_sdk/include'
if(-not (Test-Path -LiteralPath (Join-Path $sdk 'scssdk_telemetry.h'))){throw 'SCS SDK headers not found.'}
$build=Join-Path $root '.build'
$tests=Join-Path $build 'tests'
$package=Join-Path $build 'package'
New-Item -ItemType Directory -Path $tests -Force | Out-Null
if(Test-Path -LiteralPath $package) {
    $resolved=(Resolve-Path -LiteralPath $package).Path
    if($resolved -ne [IO.Path]::GetFullPath((Join-Path $root '.build/package'))){throw 'Unexpected package staging path.'}
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
$out=Join-Path $package 'bin/win_x64/plugins'
New-Item -ItemType Directory -Path $out -Force | Out-Null
$common=@('-std=c++17','-O2','-Wall','-Wextra','-Werror','-static',
    "-ffile-prefix-map=$root=.","-fmacro-prefix-map=$root=.","-fdebug-prefix-map=$root=.")
& $Compiler @common -o (Join-Path $tests 'logic-tests.exe') (Join-Path $PSScriptRoot 'src/tests.cpp')
if($LASTEXITCODE){throw 'Logic test build failed.'}
Push-Location $tests
try {
    & './logic-tests.exe'
    if($LASTEXITCODE){throw 'Logic tests failed.'}
} finally {Pop-Location}
& $Compiler @common -municode -o (Join-Path $tests 'logging-tests.exe') (Join-Path $PSScriptRoot 'src/logging-tests.cpp')
if($LASTEXITCODE){throw 'Logging test build failed.'}
Push-Location $tests
try {
    & './logging-tests.exe'
    if($LASTEXITCODE){throw 'Logging tests failed.'}
} finally {Pop-Location}
$dll=Join-Path $out 'road_trip_shift.dll'
& $Compiler @common -shared -s -static-libgcc -static-libstdc++ -fno-exceptions -fno-rtti '-Wl,--no-insert-timestamp' ("-I$sdk") -o $dll (Join-Path $PSScriptRoot 'src/road_trip_shift.cpp') -luser32 -lshell32 -lole32 -lpsapi
if($LASTEXITCODE){throw 'Plugin build failed.'}
& $Compiler @common -static-libgcc -static-libstdc++ -fno-exceptions -fno-rtti -municode ("-I$sdk") -o (Join-Path $tests 'native-tests.exe') (Join-Path $PSScriptRoot 'src/native-tests.cpp') -luser32 -lshell32 -lole32 -lpsapi
if($LASTEXITCODE){throw 'Native test build failed.'}
# Resolve before changing working directory; relative input paths remain valid.
if($GameExe){$GameExe=(Resolve-Path -LiteralPath $GameExe).Path}
Push-Location $tests
try {
    if($GameExe){& './native-tests.exe' $GameExe}
    else {& './native-tests.exe'}
    if($LASTEXITCODE){throw 'Native tests failed.'}
} finally {Pop-Location}

$configDir=Join-Path $out 'road_trip_shift'
New-Item -ItemType Directory -Path $configDir -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'config/road_trip_shift.ini') -Destination $configDir -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Install.ps1') -Destination $package -Force
$guideDir=Join-Path $package 'docs'
$licenseDir=Join-Path $package 'licenses'
New-Item -ItemType Directory -Path $guideDir,$licenseDir -Force | Out-Null
foreach($name in @('INSTALL.md','INSTALL_RU.md')) {
    Copy-Item -LiteralPath (Join-Path $root "docs/$name") -Destination $guideDir -Force
}
Copy-Item -Path (Join-Path $root 'third_party/licenses/*') -Destination $licenseDir -Force
@"
# Road Trip Shift $version experimental

[Русская версия](README_RU.md)

Close ATS, then copy the included bin folder into your game directory. Keep an existing INI when updating. The DLL alone uses built-in defaults.

[Installation, compatibility, settings and removal](docs/INSTALL.md) · [Установка на русском](docs/INSTALL_RU.md)

Keyboard accelerator input, signature resolution and cruise protection still need driving validation. Retain the licenses folder when redistributing this package. Tested on ATS 1.62.1.1 (Windows x64). BUILD-INFO.json records the tested EXE and DLL hash.
"@ | Set-Content -LiteralPath (Join-Path $package 'README.md') -Encoding utf8NoBOM
@"
# Road Trip Shift $version — экспериментальная сборка

[На английском](README.md)

Закройте ATS и скопируйте папку bin из архива в каталог игры. При обновлении сохраните существующий INI. Для работы достаточно DLL со встроенными настройками.

[Установка, совместимость, настройки и удаление](docs/INSTALL_RU.md) · [Установка на английском](docs/INSTALL.md)

Клавиатурный ввод газа, поиск адресов и защита круиза ещё требуют проверки в поездке. При распространении сохраняйте папку licenses. Проверено на ATS 1.62.1.1 (Windows x64). BUILD-INFO.json содержит хеши проверенного EXE и DLL.
"@ | Set-Content -LiteralPath (Join-Path $package 'README_RU.md') -Encoding utf8NoBOM
$info=[ordered]@{
    version=$version
    channel='experimental'
    built_at_utc=[DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
    tested_game='American Truck Simulator 1.62.1.1 Windows x64'
    tested_exe_sha256=$testedHash
    address_resolution='instruction_signatures'
    version_hash_restriction=$false
    dll_sha256=(Get-FileHash -LiteralPath $dll).Hash
    native_image_checked=[bool]$GameExe
    native_sites=20
    checks=@('Input/mode and cruise-guard logic','SDK vehicle scope and mode provenance','Configured keyboard accelerator, controller exclusion and fresh per-frame input',
        'Full pause/resume, timer/vehicle/focus cancellation and release gating',
        'Smart Throttle ramp confirmation and unconfirmed stroke rejection','Native candidate and actual gear diagnostics','Multi-frame physical release between taps','Economy cruise speed recovery uses current Normal hooks and guard, then restores selection','ACC traffic recovery and keyboard kickdown priority on cruise','High Power precedence across all selected modes, cruise options and logging; selection changes while held',
        'Unpublished hook-page cleanup and tracked published retention','Process memory and handle sampling',
        'Missing/ambiguous and relocated signature resolution','Native condition and chooser execution','Isolated patch restoration','Telemetry/log lifecycle','Bounded log rotation and retention')
    driving_validation='0.2.2 base behavior road-tested; keyboard accelerator input, signature resolution and cruise guard road validation pending'
}
$manifest=Join-Path $package 'BUILD-INFO.json'
$info | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $manifest -Encoding utf8NoBOM
# Keep archive text independent of checkout line-ending settings.
foreach($file in Get-ChildItem -LiteralPath $package -Recurse -File | Where-Object Extension -ne '.dll') {
    $text=[IO.File]::ReadAllText($file.FullName).Replace("`r`n","`n")
    [IO.File]::WriteAllText($file.FullName,$text,[Text.UTF8Encoding]::new($false))
}
$archive=Join-Path $build "road_trip_shift_${version}_win64.zip"
if(-not $SkipPackage){Compress-Archive -Path (Join-Path $package '*') -DestinationPath $archive -Force}
if($Publish) {
    $release=Join-Path $root 'release'
    New-Item -ItemType Directory -Path $release -Force | Out-Null
    Copy-Item -LiteralPath $dll,$manifest,$archive -Destination $release -Force
    # Keep only the current installation archive in the repository's release folder.
    foreach($old in Get-ChildItem -LiteralPath $release -File -Filter 'road_trip_shift_*_win64.zip') {
        if($old.Name -ne (Split-Path $archive -Leaf)){Remove-Item -LiteralPath $old.FullName}
    }
    $checksums=foreach($name in @('road_trip_shift.dll',"road_trip_shift_${version}_win64.zip",'BUILD-INFO.json')) {
        '{0}  {1}' -f (Get-FileHash -LiteralPath (Join-Path $release $name)).Hash.ToLowerInvariant(),$name
    }
    $checksums | Set-Content -LiteralPath (Join-Path $release 'SHA256SUMS.txt') -Encoding utf8NoBOM
    & (Join-Path $root 'scripts/Check-PublicFiles.ps1')
}
Write-Output "PASS: Road Trip Shift $version built; installation is a separate step."
Get-FileHash -LiteralPath $dll | Select-Object Algorithm,Hash
