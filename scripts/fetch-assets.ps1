$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$assetRoot = Join-Path $repoRoot 'assets'
$downloadRoot = Join-Path $repoRoot 'out\downloads\restore'
$manifest = Get-Content -LiteralPath (Join-Path $assetRoot 'sources.json') -Raw -Encoding UTF8 | ConvertFrom-Json
New-Item -ItemType Directory -Path $downloadRoot -Force | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
foreach ($entry in $manifest.files) {
    $destination = Join-Path $assetRoot $entry.destination
    if ((Test-Path -LiteralPath $destination) -and (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -eq $entry.sha256) {
        Write-Output "Verified $($entry.destination)"
        continue
    }
    $temporary = Join-Path $downloadRoot ([System.IO.Path]::GetFileName($entry.destination))
    if ($entry.archive) {
        $archivePath = Join-Path $downloadRoot $entry.archive
        Invoke-WebRequest -UseBasicParsing -Uri $entry.url -OutFile $archivePath
        $archive = [System.IO.Compression.ZipFile]::OpenRead($archivePath)
        try {
            $member = $archive.GetEntry($entry.entry)
            if (-not $member) { throw "Missing archive entry: $($entry.entry)" }
            # Extract only the expected file; never arbitrary archive paths.
            [System.IO.Compression.ZipFileExtensions]::ExtractToFile($member, $temporary, $true)
        } finally { $archive.Dispose() }
    } else { Invoke-WebRequest -UseBasicParsing -Uri $entry.url -OutFile $temporary }
    if ((Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash -ne $entry.sha256) {
        throw "Checksum mismatch for $($entry.destination); original asset may have changed."
    }
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath $temporary -Destination $destination
    Write-Output "Imported $($entry.destination)"
}
