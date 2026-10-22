#Requires -Version 7.0
param(
    [string]$InputDirectory,
    [string]$GuiBuildDirectory,
    [string]$WorkerBuildDirectory,
    [string]$NvidiaSdkLicense,
    [string]$NeuralRenderingDllForTesting,
    [string]$CMake
)
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path -LiteralPath (Split-Path -Parent $PSScriptRoot)).Path
$releaseRoot = Join-Path $repoRoot 'release'
$destination = Join-Path $releaseRoot 'ngx'
$workRoot = Join-Path $repoRoot 'build-local-release'
$next = Join-Path $workRoot 'next'
$previous = Join-Path $workRoot 'previous'
$logRoot = Join-Path $workRoot 'logs'
if (!$InputDirectory) { $InputDirectory = $destination }
if (!$GuiBuildDirectory) { $GuiBuildDirectory = Join-Path $repoRoot 'build-rc-gui' }
if (!$WorkerBuildDirectory) { $WorkerBuildDirectory = Join-Path $repoRoot 'build-rc-worker' }
$inputRoot = (Resolve-Path -LiteralPath $InputDirectory).Path
$guiBuild = (Resolve-Path -LiteralPath $GuiBuildDirectory).Path
$workerBuild = (Resolve-Path -LiteralPath $WorkerBuildDirectory).Path
if ($inputRoot.StartsWith($workRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Runtime/dependency input must be outside the temporary build staging directory.'
}

function CacheValue([string]$Directory, [string]$Key) {
    $pattern = '^' + [regex]::Escape($Key) + ':[^=]+=(.*)$'
    foreach ($line in Get-Content -LiteralPath (Join-Path $Directory 'CMakeCache.txt')) {
        if ($line -match $pattern) { return $Matches[1] }
    }
    return ''
}
function SamePath([string]$A, [string]$B) {
    return [IO.Path]::GetFullPath($A).TrimEnd('\').Equals(
        [IO.Path]::GetFullPath($B).TrimEnd('\'), [StringComparison]::OrdinalIgnoreCase)
}
function AssertPlainDirectory([string]$Directory) {
    if (!(Test-Path -LiteralPath $Directory)) { return }
    $items = @((Get-Item -LiteralPath $Directory -Force)) + @(Get-ChildItem -LiteralPath $Directory -Recurse -Force)
    foreach ($item in $items) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "Refusing to replace or remove a directory containing links: $Directory"
        }
    }
}
function AssertNotRunning([string[]]$Directories) {
    foreach ($process in [Diagnostics.Process]::GetProcesses()) {
        if ($process.ProcessName -notin @('chiaki','nvngx','nvngx.dll')) { continue }
        try { $executable = $process.MainModule.FileName }
        catch { throw 'Close running Chiaki/worker processes before replacing the local release.' }
        foreach ($directory in $Directories) {
            $prefix = [IO.Path]::GetFullPath($directory).TrimEnd('\') + '\'
            if ($executable.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
                throw "Close the application running from $directory before rebuilding it."
            }
        }
    }
}
function RemoveManagedDirectory([string]$Directory) {
    $fullPath = [IO.Path]::GetFullPath($Directory)
    if (!(SamePath $fullPath $next) -and !(SamePath $fullPath $previous)) {
        throw 'Cleanup is limited to the local build staging/rollback directories.'
    }
    if (!$fullPath.StartsWith($workRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Cleanup path escaped the local build directory.'
    }
    AssertPlainDirectory $fullPath
    AssertNotRunning @($fullPath)
    if (Test-Path -LiteralPath $fullPath) { Remove-Item -LiteralPath $fullPath -Recurse -Force }
}
function RunBuild([string]$Name, [string[]]$BuildArguments) {
    Write-Host "[App][Build] $Name"
    & $CMake @BuildArguments 2>&1 | Tee-Object -FilePath (Join-Path $logRoot ($Name + '.log'))
    if ($LASTEXITCODE) { throw "$Name failed; see build-local-release/logs/$Name.log" }
}

if (!(SamePath (CacheValue $guiBuild 'CMAKE_HOME_DIRECTORY') $repoRoot) -or
    !(SamePath (CacheValue $workerBuild 'CMAKE_HOME_DIRECTORY') (Join-Path $repoRoot 'gui/enhancement'))) {
    throw 'Configure both build trees against this checkout before rebuilding.'
}
if ((CacheValue $guiBuild 'CMAKE_BUILD_TYPE') -ne 'Release') { throw 'The GUI build must be configured as Release.' }
if (!$CMake) { $CMake = CacheValue $guiBuild 'CMAKE_COMMAND' }
$CMake = (Get-Command $CMake -ErrorAction Stop).Source
if (!$NvidiaSdkLicense) {
    $sdk = CacheValue $workerBuild 'CHIAKI_NGX_SDK'
    if (!$sdk) { throw 'Supply -NvidiaSdkLicense or configure CHIAKI_NGX_SDK in the worker build.' }
    $NvidiaSdkLicense = Join-Path $sdk 'LICENSE.txt'
}
$NvidiaSdkLicense = (Resolve-Path -LiteralPath $NvidiaSdkLicense).Path
if (!$NeuralRenderingDllForTesting) {
    $existingNr = Join-Path $inputRoot 'runtimes/nvngx_dlssnr.dll'
    if (Test-Path -LiteralPath $existingNr -PathType Leaf) { $NeuralRenderingDllForTesting = $existingNr }
}
if ($NeuralRenderingDllForTesting) {
    $nrSource = (Resolve-Path -LiteralPath $NeuralRenderingDllForTesting).Path
    $manifest = Get-Content -LiteralPath (Join-Path $inputRoot 'runtimes/runtime-manifest.json') -Raw | ConvertFrom-Json
    $nrPin = $manifest.packages.neural.'nvngx_dlssnr.dll'
    $nrHash = (Get-FileHash -LiteralPath $nrSource -Algorithm SHA256).Hash.ToLowerInvariant()
    if (!$nrPin -or $nrHash -notin @($nrPin)) { throw '[RTX][Build] The test NR runtime does not match the staged manifest.' }
}
AssertNotRunning @($destination, $next, $previous)
foreach ($directory in @($releaseRoot, $workRoot)) {
    if ((Test-Path -LiteralPath $directory) -and
        ((Get-Item -LiteralPath $directory -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Managed build/release root must not be a link: $directory"
    }
}
AssertPlainDirectory $destination
New-Item -ItemType Directory -Path $logRoot -Force | Out-Null
RemoveManagedDirectory $next
RemoveManagedDirectory $previous

$savedPath = $env:PATH
try {
    $env:PATH = (Split-Path -Parent $CMake) + ';' + $env:PATH
    RunBuild 'gui' @('--build', $guiBuild, '--config', 'Release', '--parallel', '4')
    RunBuild 'worker' @('--build', $workerBuild, '--config', 'Release', '--parallel', '2')
    # The public packaging helper always omits NR and creates no ZIP unless requested.
    & (Join-Path $PSScriptRoot 'Package-EnhancementRelease.ps1') -InputDirectory $inputRoot `
        -DestinationDirectory $next -NvidiaSdkLicense $NvidiaSdkLicense |
        Tee-Object -FilePath (Join-Path $logRoot 'package.log')
    $vendorLogs = Join-Path $inputRoot 'runtimes/sm86-proxy/dlssg_sm86/logs'
    if (Test-Path -LiteralPath $vendorLogs -PathType Container) {
        Copy-Item -LiteralPath $vendorLogs -Destination (Join-Path $next 'runtimes/sm86-proxy/dlssg_sm86/logs') -Recurse
    }
    Copy-Item -LiteralPath (Join-Path $guiBuild 'gui/chiaki.exe') -Destination (Join-Path $next 'chiaki.exe') -Force
    Copy-Item -LiteralPath (Join-Path $workerBuild 'Release/nvngx.dll') -Destination (Join-Path $next 'enhancement/nvngx.dll') -Force
    $localNr = [bool]$NeuralRenderingDllForTesting
    if ($localNr) {
        Copy-Item -LiteralPath $nrSource -Destination (Join-Path $next 'runtimes/nvngx_dlssnr.dll')
        if ((Get-FileHash -LiteralPath (Join-Path $next 'runtimes/nvngx_dlssnr.dll') -Algorithm SHA256).Hash.ToLowerInvariant() -ne $nrHash) {
            throw '[RTX][Build] Copied NR runtime failed verification.'
        }
        $readmePath = Join-Path $next 'README.md'
        $readme = [IO.File]::ReadAllText($readmePath)
        $readme = $readme.Replace('3. For optional experimental **Neural Rendering**, supply your own compatible **`nvngx_dlssnr.dll`** in **`runtimes/`** beside `chiaki.exe`.',
            '3. This **local test build** already includes the compatible Neural Rendering runtime in **`runtimes/`**.')
        [IO.File]::WriteAllText($readmePath, $readme, [Text.UTF8Encoding]::new($false))
        '[RTX][Build] Matching NR runtime included for local testing.' | Tee-Object -FilePath (Join-Path $logRoot 'runtime.log')
    }
    $head = (& git -C $repoRoot rev-parse HEAD).Trim()
    if ($LASTEXITCODE) { throw 'Could not read source revision.' }
    $changes = @(& git -C $repoRoot status --porcelain --untracked-files=normal)
    if ($LASTEXITCODE) { throw 'Could not read source status.' }
    $info = [ordered]@{
        variant = 'ngx'; base_commit = $head; build_type = 'Release'
        built_at = [DateTimeOffset]::UtcNow.ToOffset([TimeSpan]::FromHours(7)).ToString('o')
        source_state = $(if ($changes.Count) { 'working tree with local changes' } else { 'clean' })
        gui_sha256 = (Get-FileHash -LiteralPath (Join-Path $next 'chiaki.exe') -Algorithm SHA256).Hash.ToLowerInvariant()
        worker_sha256 = (Get-FileHash -LiteralPath (Join-Path $next 'enhancement/nvngx.dll') -Algorithm SHA256).Hash.ToLowerInvariant()
        runtime_manifest_sha256 = (Get-FileHash -LiteralPath (Join-Path $next 'runtimes/runtime-manifest.json') -Algorithm SHA256).Hash.ToLowerInvariant()
        update = 'single-local-release'; local_test_build = $true
        bundled_runtimes = [ordered]@{ frame_generation = $true; sm86 = $true; neural = $localNr }
        runtime_directory = 'runtimes'; worker_path = 'enhancement/nvngx.dll'
        external_runtime_policy = $(if ($localNr) { 'Supplied NR runtime included for local testing; public packaging omits NR.' } else { 'NR is user supplied; FG and SM86 are bundled.' })
    }
    $info | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $next 'build-info.json') -Encoding utf8
    AssertNotRunning @($destination, $next, $previous)
    New-Item -ItemType Directory -Path $releaseRoot -Force | Out-Null
    # Only these exact paths can be moved, and all were checked for containment/links.
    if (Test-Path -LiteralPath $destination) { Move-Item -LiteralPath $destination -Destination $previous }
    try { Move-Item -LiteralPath $next -Destination $destination }
    catch {
        if (Test-Path -LiteralPath $previous) { Move-Item -LiteralPath $previous -Destination $destination }
        throw
    }
    RemoveManagedDirectory $previous
    [IO.File]::WriteAllText((Join-Path $repoRoot 'Launch-ReleaseCandidate.cmd'),
        "@echo off`r`ncall `"%~dp0release\ngx\Launch-Ngx.cmd`" %*`r`n", [Text.UTF8Encoding]::new($false))
    Write-Host '[App][Build] Updated release/ngx. No archive created.'
    [ordered]@{ destination = $destination; nr_for_local_testing = $localNr; archive_created = $false } | ConvertTo-Json
} finally { $env:PATH = $savedPath }
