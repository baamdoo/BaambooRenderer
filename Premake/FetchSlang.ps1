$ErrorActionPreference = 'Stop'

# Pinned Slang release. The version lives only in premake5.lua (SlangVersion); this file pins each version's hash, the
# one GitHub publishes for the release asset. Bump to an official release only, and add its published hash here.
$hashes = @{
    '2026.19' = 'fc922f214b5d115f663932e59e80e0f6b6cc6fae140979cf307da6459b6f8f33'
}

$root    = Split-Path -Parent $PSScriptRoot
$pin     = Select-String -LiteralPath (Join-Path $root 'premake5.lua') -Pattern '^SlangVersion = "([0-9.]+)"'
# Lua keeps the last of several lines while this script reads the first, so require exactly one.
if (@($pin).Count -ne 1) { throw 'premake5.lua must have exactly one SlangVersion = "<version>" line.' }
$version = $pin.Matches[0].Groups[1].Value
$sha256  = $hashes[$version]
if (-not $sha256) { throw "No published hash pinned for Slang $version in Premake/FetchSlang.ps1." }

$dest    = Join-Path $root "External/slang-$version"
if (Test-Path -LiteralPath $dest) { exit 0 }

$url     = "https://github.com/shader-slang/slang/releases/download/v$version/slang-$version-windows-x86_64.zip"
$zip     = "$dest.download.zip"
$staging = "$dest.partial"

New-Item -ItemType Directory -Force (Split-Path -Parent $dest) | Out-Null
try {
    Write-Host "Fetching Slang $version ..."
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $ProgressPreference = 'SilentlyContinue'
    Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing

    $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $zip).Hash.ToLowerInvariant()
    if ($actual -ne $sha256) {
        throw "Slang $version archive hash mismatch (expected $sha256, got $actual)."
    }

    if (Test-Path -LiteralPath $staging) { Remove-Item -Recurse -Force -LiteralPath $staging }
    Expand-Archive -LiteralPath $zip -DestinationPath $staging

    # The release zip may wrap everything in one top-level folder.
    $content = $staging
    $entries = @(Get-ChildItem -LiteralPath $staging -Force)
    if ($entries.Count -eq 1 -and $entries[0].PSIsContainer) { $content = $entries[0].FullName }
    if (-not (Test-Path -LiteralPath (Join-Path $content 'include/slang.h'))) {
        throw "Slang $version archive layout is unexpected (include/slang.h not found)."
    }

    # Move into place last, so that $dest exists only when the extraction is complete.
    Move-Item -LiteralPath $content -Destination $dest
    Write-Host "Slang $version -> $dest"
} finally {
    Remove-Item -Force -LiteralPath $zip -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $staging) { Remove-Item -Recurse -Force -LiteralPath $staging }
}
