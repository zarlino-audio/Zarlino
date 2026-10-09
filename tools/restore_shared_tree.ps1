# Restores the `shared/` library from a Copilot checkpoint snapshot.
#
# The workspace reset on 2026-09-01 removed the untracked `shared/` tree that the
# Zarlino UI design system lives in. Those files were never committed on this
# branch, but they are still present in checkpoint snapshots, so they can be
# recovered byte-for-byte.
#
# Only files that are MISSING on disk are written: anything already present is
# left untouched so a newer working copy is never clobbered.
[CmdletBinding()]
param(
    [string] $Commit = '71758c69d12dbcb725747bf1863cc67a87e05dcb',
    [string] $Tree   = 'shared',
    [switch] $WhatIfOnly
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

# `git archive` would overwrite existing files, so walk the tree and check first.
$paths = @(git ls-tree -r --name-only $Commit -- $Tree)
if ($paths.Count -eq 0) { throw "No files under '$Tree' in commit $Commit" }

$restored = 0
$skipped  = 0

foreach ($p in $paths) {
    $local = Join-Path $repoRoot ($p -replace '/', '\')

    if (Test-Path -LiteralPath $local) { $skipped++; continue }

    if ($WhatIfOnly) { Write-Output "would restore $p"; $restored++; continue }

    $dir = Split-Path -Parent $local
    if (-not (Test-Path -LiteralPath $dir)) {
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
    }

    # `git show` piped through cmd redirection keeps the bytes exactly as stored:
    # no BOM, no CRLF translation, no encoding conversion.
    $spec = "$Commit`:$p"
    cmd /c "git show $spec > `"$local`""
    if (-not (Test-Path -LiteralPath $local)) { throw "Failed to restore $p" }

    $restored++
}

Write-Output "restored=$restored skipped(existing)=$skipped tree=$Tree commit=$Commit"
