# Convenience entrypoint; settings and hand corrections live in the reusable profile.
param([switch]$Overwrite, [switch]$PreviewOnly)
& (Join-Path $PSScriptRoot 'make-team-masks.ps1') -Profile (Join-Path $PSScriptRoot 'mask-profiles/novice.json') -Overwrite:$Overwrite -PreviewOnly:$PreviewOnly
