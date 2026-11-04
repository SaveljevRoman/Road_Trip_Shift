param([switch]$History)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$privatePattern='(?i)(?<![a-z0-9])[a-z]:[\\/]|-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----|\bgh[pousr]_[a-zA-Z0-9]{30,}|\bgithub_pat_[a-zA-Z0-9_]{30,}'
function Check-Text([string]$Text,[string]$Label) {
    if($Text -match $privatePattern){throw "Private path or credential-like content found in $Label"}
}
function Check-Links([string]$Text,[string]$Label,[scriptblock]$Exists) {
    foreach($link in [regex]::Matches($Text,'\[[^\]]+\]\(([^)\s]+)\)')) {
        $target=$link.Groups[1].Value
        if($target -match '^[a-zA-Z][a-zA-Z0-9+.-]*:' -or $target.StartsWith('#')){continue}
        $target=[Uri]::UnescapeDataString(($target -split '#')[0])
        if(-not (& $Exists $target)){throw "Broken relative link in ${Label}: $target"}
    }
}
$files=@(& git -C $root ls-files --cached --others --exclude-standard)
if($LASTEXITCODE){throw 'Cannot list repository files.'}
$textCount=0
foreach($file in $files | Sort-Object -Unique) {
    $path=Join-Path $root $file
    if(-not (Test-Path -LiteralPath $path -PathType Leaf)){continue}
    if($file -match '\.(dll|zip)$'){continue}
    $text=[IO.File]::ReadAllText($path)
    Check-Text $text $file
    if($file.EndsWith('.md')) {
        $parent=Split-Path $path -Parent
        Check-Links $text $file {param($target) Test-Path -LiteralPath (Join-Path $parent $target)}
    }
    $textCount++
}
$release=Join-Path $root 'release'
$info=Get-Content -LiteralPath (Join-Path $release 'BUILD-INFO.json') -Raw | ConvertFrom-Json
if(-not $info.native_image_checked -or $info.native_sites -ne 20){throw 'Published build lacks full native verification.'}
$dll=Join-Path $release 'road_trip_shift.dll'
$bytes=[IO.File]::ReadAllBytes($dll)
Check-Text ([Text.Encoding]::ASCII.GetString($bytes)) 'release DLL ASCII strings'
Check-Text ([Text.Encoding]::Unicode.GetString($bytes)) 'release DLL UTF-16 strings'
if((Get-FileHash -LiteralPath $dll).Hash -ne $info.dll_sha256){throw 'Published DLL differs from its manifest.'}
foreach($line in Get-Content -LiteralPath (Join-Path $release 'SHA256SUMS.txt')) {
    if($line -notmatch '^([a-f0-9]{64})  ([^/\\]+)$'){throw 'Invalid checksum entry.'}
    if((Get-FileHash -LiteralPath (Join-Path $release $Matches[2])).Hash -ne $Matches[1]){throw 'Published checksum mismatch.'}
}
$archive=Join-Path $release ('road_trip_shift_'+$info.version+'_win64.zip')
$zip=[IO.Compression.ZipFile]::OpenRead($archive)
try {
    $entries=@{}
    foreach($entry in $zip.Entries) {
        if($entry.FullName.EndsWith('/')){continue}
        $name=$entry.FullName.Replace('\','/')
        $stream=$entry.Open();$memory=[IO.MemoryStream]::new()
        try{$stream.CopyTo($memory);$entries[$name]=$memory.ToArray()}finally{$stream.Dispose();$memory.Dispose()}
    }
    $expected=@('README.md','README_RU.md','docs/INSTALL.md','docs/INSTALL_RU.md','Install.ps1','BUILD-INFO.json',
        'bin/win_x64/plugins/road_trip_shift.dll','bin/win_x64/plugins/road_trip_shift/road_trip_shift.ini')
    $expected+=@(Get-ChildItem -LiteralPath (Join-Path $root 'third_party/licenses') -File | ForEach-Object {'licenses/'+$_.Name})
    if(@(Compare-Object ($expected | Sort-Object) ($entries.Keys | Sort-Object)).Count){throw 'Unexpected/missing files in installation ZIP.'}
    foreach($name in $entries.Keys) {
        if($name.EndsWith('.dll')) {
            $digest=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($entries[$name]))
            if($digest -ne $info.dll_sha256){throw 'ZIP DLL differs from standalone DLL.'}
            continue
        }
        $text=[Text.Encoding]::UTF8.GetString($entries[$name]);Check-Text $text "ZIP/$name"
        if($name.EndsWith('.md')) {
            $entryParent=Split-Path $name -Parent
            Check-Links $text "ZIP/$name" {
                param($target)
                $virtualRoot=[IO.Path]::GetFullPath((Join-Path $root '.build/package'))
                $full=[IO.Path]::GetFullPath((Join-Path (Join-Path $virtualRoot $entryParent) $target))
                $relative=[IO.Path]::GetRelativePath($virtualRoot,$full).Replace('\','/')
                $entries.ContainsKey($relative)
            }
        }
        # Distribution copies must match their tracked sources.
        $original=switch -Regex ($name) {
            '^docs/' {Join-Path $root $name; break}
            '^licenses/' {Join-Path $root ('third_party/'+$name); break}
            '^Install.ps1$' {Join-Path $root 'plugin/Install.ps1'; break}
            '\.ini$' {Join-Path $root 'plugin/config/road_trip_shift.ini'; break}
            '^BUILD-INFO.json$' {Join-Path $release 'BUILD-INFO.json'; break}
        }
        if($original -and $text.Replace("`r`n","`n") -cne [IO.File]::ReadAllText($original).Replace("`r`n","`n")){throw "Stale distribution copy: $name"}
    }
} finally {$zip.Dispose()}
if($History) {
    $objects=@(& git -C $root rev-list --objects main)
    if($LASTEXITCODE){throw 'Cannot inspect main history.'}
    foreach($object in $objects) {
        $parts=$object -split ' ',2
        if($parts.Count -lt 2 -or $parts[1] -match '\.(dll|zip)$'){continue}
        $kind=& git -C $root cat-file -t $parts[0]
        if($kind -ne 'blob'){continue}
        $text=(& git -C $root cat-file blob $parts[0]) -join "`n"
        Check-Text $text ('history blob '+$parts[0])
    }
}
Write-Output "PASS: $textCount public text files, relative links, DLL strings, ZIP contents and checksums."
if($History){Write-Output 'PASS: main history contains no detected absolute machine paths or credential patterns.'}
