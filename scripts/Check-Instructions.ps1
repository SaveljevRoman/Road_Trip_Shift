param([switch]$Staged)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if($Staged) {
    $paths=@(& git -C $root diff --cached --name-only -- AGENTS.md CLAUDE.md)
    if($LASTEXITCODE){throw 'Cannot inspect Git index'}
    if(($paths -contains 'AGENTS.md') -xor ($paths -contains 'CLAUDE.md')) {throw 'Stage AGENTS.md and CLAUDE.md together.'}
    $a=(& git -C $root show ':AGENTS.md') -join "`n"
    if($LASTEXITCODE){throw 'AGENTS.md missing from index'}
    $b=(& git -C $root show ':CLAUDE.md') -join "`n"
    if($LASTEXITCODE){throw 'CLAUDE.md missing from index'}
    if($a -cne $b){throw 'Staged AGENTS.md and CLAUDE.md differ.'}
} else {
    if((Get-FileHash (Join-Path $root 'AGENTS.md')).Hash -ne (Get-FileHash (Join-Path $root 'CLAUDE.md')).Hash) {throw 'AGENTS.md and CLAUDE.md must be byte-identical.'}
}
Write-Output 'PASS: AGENTS.md and CLAUDE.md are synchronized.'
