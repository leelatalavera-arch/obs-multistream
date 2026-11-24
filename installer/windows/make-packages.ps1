# Builds the Windows .zip (manual install) and the .exe installer.
# Usage (after building + `cmake --install build_x64 --prefix release/<Config> --config <Config>`):
#   pwsh -File installer/windows/make-packages.ps1 -Configuration RelWithDebInfo
[CmdletBinding()]
param(
    [string] $Configuration = 'RelWithDebInfo'
)
$ErrorActionPreference = 'Stop'

$Root = Resolve-Path "$PSScriptRoot/../.."
$Spec = Get-Content "$Root/buildspec.json" -Raw | ConvertFrom-Json
$Name = $Spec.name
$Version = $Spec.version
$Built = Join-Path $Root "release/$Configuration/$Name"
$Out = Join-Path $Root 'release'

if (-not (Test-Path "$Built/bin/64bit/$Name.dll")) {
    throw "Plugin not built/installed at $Built. Build and run cmake --install first."
}

# ---- zip in OBS-folder layout (obs-plugins/64bit + data/obs-plugins/<name>)
$Stage = Join-Path $Out 'stage-windows'
Remove-Item $Stage -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force "$Stage/obs-plugins/64bit", "$Stage/data/obs-plugins/$Name" | Out-Null
Copy-Item "$Built/bin/64bit/$Name.dll" "$Stage/obs-plugins/64bit/"
Copy-Item "$Built/data/*" "$Stage/data/obs-plugins/$Name/" -Recurse
Copy-Item "$Root/LICENSE" "$Stage/LICENSE.txt"
Copy-Item "$PSScriptRoot/HOW-TO-INSTALL.txt" "$Stage/HOW-TO-INSTALL.txt"

$Zip = Join-Path $Out "$Name-$Version-windows-x64.zip"
Remove-Item $Zip -ErrorAction SilentlyContinue
Compress-Archive -Path "$Stage/*" -DestinationPath $Zip -CompressionLevel Optimal
Write-Host "zip: $Zip"

# ---- installer
$Iscc = @(
    $env:ISCC,
    "$env:LOCALAPPDATA/Programs/Inno Setup 6/ISCC.exe",
    "${env:ProgramFiles(x86)}/Inno Setup 6/ISCC.exe",
    "$env:ProgramFiles/Inno Setup 6/ISCC.exe"
) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $Iscc) { throw 'Inno Setup (ISCC.exe) not found. Install it: winget install JRSoftware.InnoSetup' }

& $Iscc /Q "/DAppVersion=$Version" "/DSourceDir=$(Join-Path $Out $Configuration)" "/DOutputDir=$Out" "$PSScriptRoot/obs-multistream.iss"
if ($LASTEXITCODE -ne 0) { throw "ISCC failed ($LASTEXITCODE)" }
Write-Host "installer: $(Join-Path $Out "$Name-$Version-windows-x64-installer.exe")"
Remove-Item $Stage -Recurse -Force
