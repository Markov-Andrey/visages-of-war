# Offline red-accent masks and team-color previews. Never modifies source images.
param(
    [Parameter(Mandatory=$true)][string]$Profile,
    [switch]$Overwrite,
    [switch]$PreviewOnly
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = Split-Path -Parent $PSScriptRoot
function Child-Path([string]$Parent, [string]$Relative) {
    if ([string]::IsNullOrWhiteSpace($Relative) -or [IO.Path]::IsPathRooted($Relative) -or $Relative.Contains(':')) { throw "Expected a relative path: $Relative" }
    $parentPath = [IO.Path]::GetFullPath($Parent).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
    $result = [IO.Path]::GetFullPath((Join-Path $Parent $Relative))
    if (!$result.StartsWith($parentPath, [StringComparison]::OrdinalIgnoreCase)) { throw "Path escapes its root: $Relative" }
    return $result
}
if (![IO.Path]::IsPathRooted($Profile)) { $Profile = Join-Path $repoRoot $Profile }
$profileData = Get-Content -LiteralPath $Profile -Raw -Encoding UTF8 | ConvertFrom-Json
if ($profileData.version -ne 1) { throw 'Expected mask profile version 1.' }
$artRoot = Child-Path $repoRoot $profileData.assetRoot
if (!$artRoot.StartsWith((Join-Path $repoRoot 'assets') + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'assetRoot must be inside assets/.' }
$previewRoot = Join-Path $repoRoot ('out/team-masks/' + [IO.Path]::GetFileNameWithoutExtension($Profile))
$stagingRoot = Join-Path $previewRoot 'staging'
$s = $profileData.selection
$e = $profileData.edge
$selection = [double[]]@($s.minimumRed, $s.minimumRedGreenDifference, $s.redDominance, $s.warmHueSlope, $s.warmHueOffset)
$edge = [double[]]@($e.darkRedLimit, $e.darkOtherLimit, $e.minimumRedGreenDifference, $e.redDominance, $e.warmHueSlope, $e.warmHueOffset)
foreach ($value in ($selection + $edge)) {
    if ([double]::IsNaN($value) -or [double]::IsInfinity($value) -or $value -lt 0) { throw 'Color thresholds must be finite and nonnegative.' }
}
if (@($profileData.jobs).Count -eq 0) { throw 'The profile has no jobs.' }
Add-Type -AssemblyName System.Drawing
if (!('TeamMaskAuthoring' -as [type])) {
    Add-Type -ReferencedAssemblies System.Drawing -Path (Join-Path $PSScriptRoot 'lib/TeamMaskAuthoring.cs')
}
$jobs = @()
$destinations = @{}
$sources = @{}
$stems = @{}
foreach ($job in $profileData.jobs) {
    $source = Child-Path $artRoot $job.image
    $mask = Child-Path $artRoot $job.mask
    if ([IO.Path]::GetExtension($source) -ne '.png' -or [IO.Path]::GetExtension($mask) -ne '.png') { throw 'Images and masks must be PNG files.' }
    if (@($job.size).Count -ne 2) { throw 'size requires [width,height].' }
    foreach ($value in $job.size) { if ($value -lt 1 -or $value -gt 16384 -or $value -ne [int]$value) { throw 'Invalid source dimensions.' } }
    $bitmap = [Drawing.Bitmap]::FromFile($source)
    try { if ($bitmap.Width -ne $job.size[0] -or $bitmap.Height -ne $job.size[1]) { throw "Artwork dimensions changed: $source" } }
    finally { $bitmap.Dispose() }
    if ($PreviewOnly) {
        if (!(Test-Path -LiteralPath $mask -PathType Leaf)) { throw "Missing mask: $mask" }
    } elseif ((Test-Path -LiteralPath $mask) -and !$Overwrite) { throw "Mask exists: $mask. Use -Overwrite to replace it, or -PreviewOnly to inspect it." }
    $stem = [IO.Path]::GetFileNameWithoutExtension($job.image)
    if ($destinations.ContainsKey($mask) -or $stems.ContainsKey($stem)) { throw 'Jobs require distinct mask paths and image filenames.' }
    $destinations[$mask] = $true; $sources[$source] = $true; $stems[$stem] = $true
    $background = @()
    foreach ($rectangle in $job.backgroundSeedRects) {
        if (@($rectangle).Count -ne 4) { throw 'Background seed rectangles require [x,y,width,height].' }
        foreach ($value in $rectangle) { if ($value -ne [int]$value) { throw 'Background coordinates must be integers.' } }
        $background += $rectangle
    }
    $jobs += @{ Source=$source; Mask=$mask; Settings=$job; Stem=$stem; Background=[int[]]$background }
}
foreach ($path in $destinations.Keys) { if ($sources.ContainsKey($path)) { throw "A mask must not overwrite source art: $path" } }
$manifestPath = Join-Path $artRoot 'manifest.json'
$manifest = $null
if (!$PreviewOnly -and (Test-Path -LiteralPath $manifestPath)) {
    $manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if (!$manifest.PSObject.Properties['files']) { throw 'The asset manifest must contain a files array.' }
}
[void](New-Item -ItemType Directory -Force -Path $stagingRoot)
# Generate all jobs into out/ before publishing any masks into assets/.
foreach ($job in $jobs) {
    $mask = $job.Mask
    if (!$PreviewOnly) {
        $mask = Join-Path $stagingRoot ($job.Stem + '-mask.png')
        [TeamMaskAuthoring]::Save($job.Source, $mask, $job.Settings.size[0], $job.Settings.size[1], $selection, $edge,
            [string[]]$job.Settings.excludePolygons, $job.Background)
    }
    foreach ($paint in @(@('blue',0x2874eb), @('green',0x259448), @('white',0xffffff), @('black',0))) {
        [TeamMaskAuthoring]::Preview($job.Source, $mask, (Join-Path $previewRoot ($job.Stem + '-' + $paint[0] + '.png')), $paint[1])
    }
}
if (!$PreviewOnly) {
    foreach ($job in $jobs) {
        [void](New-Item -ItemType Directory -Force -Path (Split-Path -Parent $job.Mask))
        Copy-Item -LiteralPath (Join-Path $stagingRoot ($job.Stem + '-mask.png')) -Destination $job.Mask -Force
        if ($null -ne $manifest) {
            $relative = $job.Settings.mask.Replace('\','/')
            $entry = @($manifest.files | Where-Object { $_.path -eq $relative })
            $hash = (Get-FileHash -LiteralPath $job.Mask -Algorithm SHA256).Hash
            if ($entry.Count -eq 0) { $manifest.files = @($manifest.files) + [pscustomobject]@{path=$relative; sha256=$hash} }
            else { foreach ($item in $entry) { $item.sha256 = $hash } }
        }
        Write-Output "Mask: $($job.Mask)"
    }
    if ($null -ne $manifest) {
        [IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 50) + [Environment]::NewLine, (New-Object Text.UTF8Encoding $false))
    }
}
Write-Output "Previews: $previewRoot"
