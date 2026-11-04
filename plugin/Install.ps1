param([Parameter(Mandatory=$true)][string]$GameRoot)
$ErrorActionPreference='Stop'
if(Get-Process -Name amtrucks -ErrorAction SilentlyContinue){throw 'Close ATS normally before installation. The installer never closes it.'}
$GameRoot=(Resolve-Path -LiteralPath $GameRoot).Path
$exe=Join-Path $GameRoot 'bin/win_x64/amtrucks.exe'
if(-not (Test-Path -LiteralPath $exe)){throw 'ATS installation not found.'}
$manifestPath=Join-Path $PSScriptRoot 'BUILD-INFO.json'
if(-not (Test-Path -LiteralPath $manifestPath)){throw 'Run Install.ps1 from an extracted installation ZIP.'}
$info=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$source=Join-Path $PSScriptRoot 'bin/win_x64/plugins/road_trip_shift.dll'
if((Get-FileHash -LiteralPath $source).Hash -ne $info.dll_sha256){throw 'Package DLL hash mismatch.'}
$destDir=Join-Path $GameRoot 'bin/win_x64/plugins'
$dest=Join-Path $destDir 'road_trip_shift.dll'
$configDir=Join-Path $destDir 'road_trip_shift'
$backup=Join-Path $configDir ('backups/'+[DateTime]::UtcNow.ToString('yyyyMMdd_HHmmss_fff'))
New-Item -ItemType Directory -Path $destDir,$configDir -Force | Out-Null
if(Test-Path -LiteralPath $dest) {
    New-Item -ItemType Directory -Path $backup -Force | Out-Null
    Copy-Item -LiteralPath $dest -Destination (Join-Path $backup 'road_trip_shift.dll')
}
Copy-Item -LiteralPath $source -Destination $dest -Force
$config=Join-Path $configDir 'road_trip_shift.ini'
if(-not (Test-Path -LiteralPath $config)) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'bin/win_x64/plugins/road_trip_shift/road_trip_shift.ini') -Destination $config
}
$hash=(Get-FileHash -LiteralPath $dest).Hash
if($hash -ne $info.dll_sha256){throw 'Installed DLL hash mismatch.'}
$receipt=[ordered]@{installed_at_utc=[DateTime]::UtcNow.ToString('o');version=$info.version;dll=$dest;sha256=$hash}
$receipt | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $configDir 'installation.json') -Encoding utf8NoBOM
Write-Output ('Installed Road Trip Shift {0}. Existing INI preserved. SHA-256: {1}' -f $info.version,$hash)
