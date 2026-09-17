$ErrorActionPreference = 'Stop'

try {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw 'Visual Studio Installer was not found. Install Visual Studio 2026 or 2022 with Desktop development with C++.'
    }
    $selected = $null
    foreach ($candidate in @(
        @{ Range = '[18.0,19.0)'; Action = 'vs2026'; Generator = 'Visual Studio 18 2026' },
        @{ Range = '[17.0,18.0)'; Action = 'vs2022'; Generator = 'Visual Studio 17 2022' }
    )) {
        $installation = & $vswhere -latest -products '*' -version $candidate.Range -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($LASTEXITCODE -ne 0) { throw 'Visual Studio detection failed.' }
        if ($installation) {
            $selected = $candidate
            $selected.Installation = "$installation".Trim()
            break
        }
    }
    if (-not $selected) {
        throw 'No usable Visual Studio 2026 or 2022 C++ installation was found.'
    }
    # Prefer the selected VS installation's CMake over an older copy on PATH.
    $cmakeCandidates = @(
        (Join-Path $selected.Installation 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe')
    )
    $cmakeCandidates += @(Get-Command cmake.exe -All -ErrorAction SilentlyContinue | ForEach-Object { $_.Source })
    $cmake = $null
    foreach ($candidate in $cmakeCandidates | Select-Object -Unique) {
        if (-not (Test-Path -LiteralPath $candidate)) { continue }
        $helpText = (& $candidate --help 2>&1) -join "`n"
        if ($LASTEXITCODE -eq 0 -and $helpText.Contains($selected.Generator) -and $helpText.Contains('--fresh')) {
            $cmake = $candidate
            break
        }
    }
    if (-not $cmake) {
        throw "No compatible CMake found. Install CMake 4.2+ for VS 2026 or 3.24+ for VS 2022."
    }
    $env:BAAMBOO_CMAKE = $cmake
    $env:BAAMBOO_VS_INSTANCE = $selected.Installation
    Write-Host "Using $($selected.Generator): $($selected.Installation)"
    Write-Host "CMake: $cmake"
    Push-Location $PSScriptRoot
    try {
        $ErrorActionPreference = 'Continue'
        & (Join-Path $PSScriptRoot 'Premake/premake5.exe') $selected.Action 2>&1 | ForEach-Object { Write-Host "$_" }
        $premakeExit = $LASTEXITCODE
        $ErrorActionPreference = 'Stop'
        if ($premakeExit -ne 0) { throw "Project generation failed (exit code $premakeExit)." }
    } finally {
        Pop-Location
    }
    exit 0
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
