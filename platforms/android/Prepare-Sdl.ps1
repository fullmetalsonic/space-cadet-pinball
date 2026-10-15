$ErrorActionPreference = 'Stop'
$vendorRoot = Join-Path $PSScriptRoot 'vendor'
New-Item -ItemType Directory -Force -Path $vendorRoot | Out-Null
$packages = @(
    @{Name='SDL2-2.30.12'; Url='https://www.libsdl.org/release/SDL2-2.30.12.zip'; Hash='aa2808d0f2dc6b383c6689bf6d166e2de62db4d58be989e4b052acb31df0fba3'},
    @{Name='SDL2_mixer-2.8.1'; Url='https://www.libsdl.org/projects/SDL_mixer/release/SDL2_mixer-2.8.1.zip'; Hash='69816beec6b81dc7d5d64ed32600a66c4452abedfEce09c5b62613bceac6fb83'}
)
foreach ($package in $packages) {
    $archive = Join-Path $vendorRoot ($package.Name + '.zip')
    if (!(Test-Path -LiteralPath $archive)) { Invoke-WebRequest -Uri $package.Url -OutFile $archive -TimeoutSec 60 }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $package.Hash) { throw "Release archive hash mismatch: $($package.Name)" }
    $source = Join-Path $vendorRoot $package.Name
    if (!(Test-Path -LiteralPath (Join-Path $source 'CMakeLists.txt'))) { Expand-Archive -LiteralPath $archive -DestinationPath $vendorRoot }
    Write-Output "$($package.Name): release source verified"
}
