#Requires -Version 7.0
param(
    [Parameter(Mandatory = $true)][string]$InputDirectory,
    [Parameter(Mandatory = $true)][string]$DestinationDirectory,
    [Parameter(Mandatory = $true)][string]$NvidiaSdkLicense,
    [string]$ArchivePath
)
$ErrorActionPreference = 'Stop'
$sourceRoot = (Resolve-Path -LiteralPath $InputDirectory).Path
$destinationRoot = [IO.Path]::GetFullPath($DestinationDirectory)
$sourcePrefix = $sourceRoot.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
if ((Test-Path -LiteralPath $destinationRoot) -or
    $destinationRoot.StartsWith($sourcePrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Use a new destination directory outside the staged input.'
}
$licenseSource = (Resolve-Path -LiteralPath $NvidiaSdkLicense).Path
$repoRoot = Split-Path -Parent $PSScriptRoot
$fsrLicense = Join-Path $repoRoot 'gui/enhancement/third-party/fsr1/LICENSE'
if (!(Test-Path -LiteralPath $fsrLicense -PathType Leaf)) { throw 'Missing AMD FSR 1 license.' }
if ($ArchivePath) {
    $archiveFullPath = [IO.Path]::GetFullPath($ArchivePath)
    if ((Test-Path -LiteralPath $archiveFullPath) -or
        $archiveFullPath.StartsWith($sourcePrefix, [StringComparison]::OrdinalIgnoreCase) -or
        $archiveFullPath.StartsWith($destinationRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Use a new archive path outside the input and output directories.'
    }
}
foreach ($required in @('chiaki.exe','enhancement/nvngx.dll','README.md',
        'runtimes/runtime-manifest.json','runtimes/sm86-proxy/LICENSE.md',
        'runtimes/sm86-proxy/THIRD_PARTY_NOTICES.txt')) {
    if (!(Test-Path -LiteralPath (Join-Path $sourceRoot $required) -PathType Leaf)) {
        throw "Incomplete staged application: $required"
    }
}
$runtimeRoot = Join-Path $sourceRoot 'runtimes'
$runtimePrefix = [IO.Path]::GetFullPath($runtimeRoot).TrimEnd('\') + '\'
$manifest = Get-Content -LiteralPath (Join-Path $runtimeRoot 'runtime-manifest.json') -Raw | ConvertFrom-Json
if ($manifest.schema -ne 2) { throw 'The release requires a version-2 runtime manifest.' }
$verified = @{}
foreach ($group in @('frame_generation','sm86')) {
    $package = $manifest.packages.$group
    if (!$package -or !$package.PSObject.Properties.Count) { throw "Missing runtime package: $group" }
    foreach ($entry in $package.PSObject.Properties) {
        $filePath = [IO.Path]::GetFullPath((Join-Path $runtimeRoot $entry.Name))
        if (!$filePath.StartsWith($runtimePrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Runtime manifest contains an escaping path.'
        }
        if (!(Test-Path -LiteralPath $filePath -PathType Leaf)) { throw "Missing bundled runtime: $($entry.Name)" }
        $hash = (Get-FileHash -LiteralPath $filePath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($hash -notin @($entry.Value)) { throw "Runtime hash mismatch: $($entry.Name)" }
        $verified[$entry.Name] = $hash
    }
}
foreach ($required in @('nvngx_dlssg.dll','sm86-proxy/version.dll',
        'sm86-proxy/dlssg_sm86/backend/sm86_backend.dll','sm86-proxy/dlssg_sm86/kernels/fg_kernels.dll')) {
    if (!$verified.ContainsKey($required)) { throw "Required bundled runtime is not pinned: $required" }
}
New-Item -ItemType Directory -Path $destinationRoot | Out-Null
$excludedNr = 0
foreach ($file in Get-ChildItem -LiteralPath $sourceRoot -Recurse -File) {
    $relative = [IO.Path]::GetRelativePath($sourceRoot, $file.FullName).Replace('\','/')
    if ($file.Name -in @('nvngx_dlssnr.dll','dlssnr.dll')) { $excludedNr++; continue }
    if ($relative -match '(^|/)(logs|capture|captures)/' -or
        $file.Extension -in @('.log','.jsonl','.dmp','.pdb','.patch') -or
        $file.Name -match '^(SESSION-HANDOFF|GAMEPLAY-CAPTURE-FINDINGS|FIRST-COMMIT|TECHNICAL|TEST-REPORT)' -or
        $file.Name -like '*-crash.txt' -or $relative -eq 'build-info.json') { continue }
    $target = Join-Path $destinationRoot $relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
    Copy-Item -LiteralPath $file.FullName -Destination $target
}
$licenses = Join-Path $destinationRoot 'licenses'
New-Item -ItemType Directory -Path $licenses -Force | Out-Null
Copy-Item -LiteralPath $licenseSource -Destination (Join-Path $licenses 'NVIDIA-RTX-SDK-LICENSE.txt') -Force
Copy-Item -LiteralPath $fsrLicense -Destination (Join-Path $licenses 'AMD-FSR1-LICENSE.txt') -Force
$opticalFlowHeader = Join-Path $repoRoot 'gui/enhancement/third-party/nvofa/nvOpticalFlowCommon.h'
$headerNotice = [regex]::Match([IO.File]::ReadAllText($opticalFlowHeader), '(?s)^/\*.*?\*/').Value
if (!$headerNotice) { throw 'Missing NVIDIA Optical Flow header notice.' }
[IO.File]::WriteAllText((Join-Path $licenses 'NVIDIA-OpticalFlow-Headers-LICENSE.txt'), $headerNotice)
# Refresh the user guide from the project, so an older staging tree cannot
# reintroduce obsolete instructions to obtain the now-bundled FG runtime.
$readme = [IO.File]::ReadAllText((Join-Path $repoRoot 'README.md'))
$readme = $readme.Replace('gui/res/chiaking-logo.svg', 'README-logo.svg').Replace(
    '](docs/diy/runtime-packaging.md)', '](https://github.com/TheHiepDang/chiaki-ngx/blob/main/docs/diy/runtime-packaging.md)')
[IO.File]::WriteAllText((Join-Path $destinationRoot 'README.md'), $readme, [Text.UTF8Encoding]::new($false))
Copy-Item -LiteralPath (Join-Path $repoRoot 'gui/res/chiaking-logo.svg') -Destination (Join-Path $destinationRoot 'README-logo.svg') -Force
# Keep source identity fields used by application diagnostics, without carrying
# local toolchain paths, handoff text or obsolete NR-enabled smoke results.
$sourceInfoPath = Join-Path $sourceRoot 'build-info.json'
$info = [ordered]@{}
if (Test-Path -LiteralPath $sourceInfoPath) {
    $sourceInfo = Get-Content -LiteralPath $sourceInfoPath -Raw | ConvertFrom-Json
    foreach ($key in @('variant','base_commit','build_type','built_at','source_state',
            'gui_sha256','worker_sha256','source_patch_sha256','limits')) {
        if ($null -ne $sourceInfo.$key) { $info[$key] = $sourceInfo.$key }
    }
}
$info['packaged_at'] = [DateTimeOffset]::UtcNow.ToOffset([TimeSpan]::FromHours(7)).ToString('o')
$info['update'] = 'bundled-fg-runtime-nr-user-supplied'
$info['runtime_manifest_sha256'] = (Get-FileHash -LiteralPath (Join-Path $destinationRoot 'runtimes/runtime-manifest.json') -Algorithm SHA256).Hash.ToLowerInvariant()
$info['external_runtime_policy'] = 'Only nvngx_dlssnr.dll is user supplied; FG and SM86 components are bundled with notices.'
$info['bundled_runtimes'] = [ordered]@{ frame_generation = $true; sm86 = $true; neural = $false }
$info['runtime_directory'] = 'runtimes'
$info['worker_path'] = 'enhancement/nvngx.dll'
$info | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $destinationRoot 'build-info.json') -Encoding utf8
if (Get-ChildItem -LiteralPath $destinationRoot -Recurse -File | Where-Object { $_.Name -in @('nvngx_dlssnr.dll','dlssnr.dll') }) {
    throw 'NR DLL unexpectedly remains in the output.'
}
if (@(Get-ChildItem -LiteralPath (Join-Path $destinationRoot 'runtimes') -Recurse -File -Filter 'nvngx_dlssg.dll').Count -ne 1) {
    throw 'The release must contain exactly one FG runtime.'
}
if ($ArchivePath) {
    New-Item -ItemType Directory -Path (Split-Path -Parent $archiveFullPath) -Force | Out-Null
    [IO.Compression.ZipFile]::CreateFromDirectory($destinationRoot, $archiveFullPath,
        [IO.Compression.CompressionLevel]::Fastest, $false)
}
[ordered]@{ destination = $destinationRoot; archive = $ArchivePath; excluded_nr_files = $excludedNr;
    fg_bundled = $true; sm86_bundled = $true; nr_bundled = $false } | ConvertTo-Json
