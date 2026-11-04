param([string]$SevenZip='')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$research=Join-Path $root 'research'
New-Item -ItemType Directory -Path $research -Force | Out-Null
$archive=Join-Path $research 'w64devkit-x64.7z.exe'
$expected='54C2BB9962E8A30C8270E34C5EFC3D5FEE579F53087FA7D435B95FBBA2AF7B2B'
if(-not $SevenZip) {
    $command=Get-Command 7z -ErrorAction SilentlyContinue
    if($command){$SevenZip=$command.Source}
    elseif($env:ProgramFiles){$SevenZip=Join-Path $env:ProgramFiles '7-Zip/7z.exe'}
}
if(-not $SevenZip -or -not (Test-Path -LiteralPath $SevenZip)){throw 'Provide -SevenZip path_to_file/7z.exe or put 7z on PATH.'}
if(-not (Test-Path -LiteralPath $archive)) {
    Invoke-WebRequest 'https://github.com/skeeto/w64devkit/releases/download/v2.11.0/w64devkit-x64-2.11.0.7z.exe' -OutFile $archive
}
if((Get-FileHash -LiteralPath $archive).Hash -ne $expected){throw 'Toolchain archive SHA-256 mismatch.'}
& $SevenZip x $archive ('-o'+(Join-Path $research 'toolchain')) -y
if($LASTEXITCODE){throw 'Toolchain extraction failed.'}
Write-Output 'Ready: run pwsh -File plugin/build.ps1'
