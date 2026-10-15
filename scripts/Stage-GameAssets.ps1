param([Parameter(Mandatory=$true)][string]$ReferenceDir)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$referenceRoot = (Resolve-Path -LiteralPath $ReferenceDir).Path
$assetRoot = Join-Path $projectRoot 'assets/game'
$allowed = @('.dat', '.wav', '.mid', '.bmp', '.inf')
$files = @(Get-ChildItem -LiteralPath $referenceRoot -File | Where-Object { $allowed -contains $_.Extension.ToLowerInvariant() })
if (!($files | Where-Object Name -IEQ 'PINBALL.DAT')) { throw 'PINBALL.DAT is required in ReferenceDir.' }
# Check every existing target before copying. Never overwrite different data.
foreach ($file in $files) {
    $destination = Join-Path $assetRoot $file.Name
    if ((Test-Path -LiteralPath $destination) -and
        ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash)) {
        throw "Existing asset differs; refusing to overwrite: $($file.Name)"
    }
}
New-Item -ItemType Directory -Force -Path $assetRoot | Out-Null
foreach ($file in $files) {
    $destination = Join-Path $assetRoot $file.Name
    if (!(Test-Path -LiteralPath $destination)) { Copy-Item -LiteralPath $file.FullName -Destination $destination }
    if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash) {
        throw "Staged asset hash mismatch: $($file.Name)"
    }
}
Write-Output "Staged $($files.Count) local game resources; SHA256 parity checked."
