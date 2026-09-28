# Keep shortcuts local to the checkout; regenerate after moving or packaging it.
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$packageDirectory = Join-Path $repoRoot 'out/package/RTS'
$applications = @('Visages of War', 'Visages Forge')
foreach ($name in $applications) {
    if (-not (Test-Path -LiteralPath (Join-Path $packageDirectory "$name.exe") -PathType Leaf)) {
        throw "Package is missing $name.exe. Run scripts/build.ps1 -Configuration Release -Package first."
    }
}
$shortcutShell = New-Object -ComObject WScript.Shell
try {
    foreach ($name in $applications) {
        $target = (Resolve-Path -LiteralPath (Join-Path $packageDirectory "$name.exe")).Path
        $shortcut = $shortcutShell.CreateShortcut((Join-Path $repoRoot "$name.lnk"))
        try {
            $shortcut.TargetPath = $target
            $shortcut.Arguments = ''
            $shortcut.WorkingDirectory = $packageDirectory
            $shortcut.IconLocation = "$target,0"
            $shortcut.Description = $name
            $shortcut.WindowStyle = 1
            $shortcut.Save()
        } finally { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shortcut) }
        Write-Host "Shortcut: $name.lnk"
    }
} finally { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shortcutShell) }
