param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [switch]$Test,
    [switch]$Benchmark,
    [switch]$Run,
    [switch]$RunForge,
    [switch]$Package
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) {
    $cmakeExe = $cmakeCommand.Source
} else {
    $vswhereExe = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhereExe)) { throw 'Install Visual Studio 2022 Build Tools: Desktop development with C++ and CMake tools.' }
    $vsRoot = & $vswhereExe -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsRoot) { throw 'Visual Studio C++ tools were not found.' }
    $cmakeExe = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
if (-not (Test-Path -LiteralPath $cmakeExe)) { throw 'CMake 3.25+ is required.' }
$ctestExe = Join-Path (Split-Path -Parent $cmakeExe) 'ctest.exe'
Push-Location -LiteralPath $repoRoot
try {
    & $cmakeExe --preset windows-x64
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
    & $cmakeExe --build --preset $Configuration.ToLowerInvariant() --parallel
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
    if ($Test) {
        & $ctestExe --preset $Configuration.ToLowerInvariant()
        if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
    }
    if ($Package) {
        # Packaging below uses the same binaries that were tested above.
        & $cmakeExe --install 'out/build/windows-x64' --config $Configuration --prefix 'out/package/RTS'
        if ($LASTEXITCODE -ne 0) { throw 'Packaging failed.' }
        & (Join-Path $PSScriptRoot 'make-shortcuts.ps1')
    }
    if ($Benchmark) {
        & $cmakeExe --build --preset $Configuration.ToLowerInvariant() --target rts_benchmarks --parallel
        if ($LASTEXITCODE -ne 0) { throw 'Benchmark build failed.' }
        & (Join-Path $repoRoot "out/build/windows-x64/$Configuration/rts_benchmarks.exe") |
            Tee-Object -FilePath (Join-Path $repoRoot "out/build/windows-x64/performance-$Configuration.txt")
        if ($LASTEXITCODE -ne 0) { throw 'Benchmark failed.' }
    }
    if ($Run) {
        # A visible window is intentional here: the user explicitly asked to play.
        Start-Process -FilePath (Join-Path $repoRoot "out/build/windows-x64/bin/$Configuration/Visages of War.exe")
    }
    if ($RunForge) {
        # Explicit request to open the editor interactively.
        Start-Process -FilePath (Join-Path $repoRoot "out/build/windows-x64/bin/$Configuration/Visages Forge.exe")
    }
} finally { Pop-Location }
