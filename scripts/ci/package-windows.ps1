#Requires -Version 7.0
param([Parameter(Mandatory)][string]$Tag)
$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '../..')
$root = (Get-Location).Path
$lock = Get-Content scripts/ci/dependencies.json -Raw | ConvertFrom-Json
& python scripts/ci/release.py download --group sm86 --folder deps/sm86.zip
if ($LASTEXITCODE) { throw '[RTX][Package] SM86 acquisition failed.' }
Expand-Archive -LiteralPath deps/sm86.zip -DestinationPath deps/sm86
New-Item -ItemType Directory -Path build/ci-stage/enhancement -Force | Out-Null
Copy-Item build/ci-worker/Release/nvngx.dll build/ci-stage/enhancement/nvngx.dll -Force
& ./scripts/Install-EnhancementRuntimes.ps1 -Destination "$root/build/ci-stage/runtimes" -Sm86ProxyDirectory "$root/deps/sm86"
# Retain compatibility with users' existing supplied NR model; never distribute it.
$manifestPath = "$root/build/ci-stage/runtimes/runtime-manifest.json"
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$manifest.packages | Add-Member -NotePropertyName neural -NotePropertyValue @{'nvngx_dlssnr.dll' = $lock.neural_runtime_sha256}
$manifest | ConvertTo-Json -Depth 9 | Set-Content -LiteralPath $manifestPath -Encoding utf8
$source = Get-Content build/ci/source.json -Raw | ConvertFrom-Json
$info = [ordered]@{ variant='ngx'; base_commit=$source.source_commit; build_type='Release';
    source_state='tagged source with documented version overlay'; release_tag=$Tag;
    release_channel=$(if ($Tag.Contains('-')) {'prerelease'} else {'release'});
    gui_sha256=(Get-FileHash build/ci-stage/chiaki.exe -Algorithm SHA256).Hash.ToLowerInvariant();
    worker_sha256=(Get-FileHash build/ci-stage/enhancement/nvngx.dll -Algorithm SHA256).Hash.ToLowerInvariant() }
$info | ConvertTo-Json | Set-Content build/ci-stage/build-info.json -Encoding utf8
Copy-Item README.md build/ci-stage/README.md
Copy-Item LICENSES build/ci-stage/licenses -Recurse
New-Item -ItemType Directory -Path dist -Force | Out-Null
& ./scripts/Package-EnhancementRelease.ps1 -InputDirectory "$root/build/ci-stage" `
    -DestinationDirectory "$root/chiaki-ngx-Win" -NvidiaSdkLicense "$root/deps/nvidia-sdk/LICENSE.txt" `
    -ArchivePath "$root/dist/chiaki-ngx-windows-x64-$Tag.zip"
& ./scripts/ci/verify-windows.ps1 -Directory "$root/chiaki-ngx-Win" -Version $Tag.Substring(1)
$iscc = "${env:ProgramFiles(x86)}/Inno Setup 6/ISCC.exe"
if (!(Test-Path -LiteralPath $iscc)) { throw '[App][Package] The windows-2022 runner must provide Inno Setup 6.' }
& $iscc /Qp "/O$root/dist" "/Fchiaki-ngx-windows-x64-$Tag-installer" scripts/chiaki-ngx.iss
if ($LASTEXITCODE) { throw '[App][Package] Installer compilation failed.' }
$installed = Join-Path $env:RUNNER_TEMP 'chiaki-installer-check'
$installer = Join-Path $root "dist/chiaki-ngx-windows-x64-$Tag-installer.exe"
$process = Start-Process -FilePath $installer -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/CURRENTUSER',"/DIR=`"$installed`"") -Wait -PassThru -WindowStyle Hidden
if ($process.ExitCode) { throw '[App][Package] Installer smoke installation failed.' }
foreach ($file in Get-ChildItem chiaki-ngx-Win -Recurse -File) {
    $relative = [IO.Path]::GetRelativePath("$root/chiaki-ngx-Win", $file.FullName)
    $target = Join-Path $installed $relative
    if (!(Test-Path -LiteralPath $target) -or (Get-FileHash $file.FullName).Hash -ne (Get-FileHash $target).Hash) {
        throw "[App][Package] Installer differs from portable payload: $relative"
    }
}
& ./scripts/ci/verify-windows.ps1 -Directory $installed -Version $Tag.Substring(1) -Installed
Write-Host '[App][Package] Portable and installer contain identical verified payloads.'
