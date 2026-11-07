# Configures a clone of this fork so upstream updates can be merged (run once per clone).
# - adds the "upstream" remote
# - registers the UTF-16 merge driver and a UTF-8 diff view for Source.cpp and miu.rc
# The attributes go to .git/info/attributes rather than .gitattributes, so a clone
# without this setup keeps treating the files as binary instead of calling a missing driver.
$ErrorActionPreference = 'Stop'

$root = git rev-parse --show-toplevel
if ($LASTEXITCODE -ne 0) { throw 'Run this inside the miu repository.' }
Set-Location $root

if (-not (git remote | Select-String -SimpleMatch -Pattern 'upstream' -Quiet)) {
    git remote add upstream https://github.com/kenjinote/miu.git
}

git config merge.utf16.name 'UTF-16 LE text merge'
git config merge.utf16.driver 'sh fork/git/utf16-merge.sh %O %A %B %L %P'
git config diff.utf16.textconv 'iconv -f UTF-16 -t UTF-8'

$attributesPath = git rev-parse --git-path info/attributes
New-Item -ItemType Directory -Force (Split-Path $attributesPath) | Out-Null
$lines = @('Source.cpp merge=utf16 diff=utf16', 'miu.rc merge=utf16 diff=utf16')
$existing = if (Test-Path $attributesPath) { @(Get-Content $attributesPath) } else { @() }
$missing = $lines | Where-Object { $existing -notcontains $_ }
if ($missing) { Add-Content -Path $attributesPath -Value $missing }

Write-Host 'Fork setup done: upstream remote, UTF-16 merge driver, and UTF-8 diff view.'
