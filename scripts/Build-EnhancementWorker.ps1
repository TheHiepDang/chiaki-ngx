param(
    [Parameter(Mandatory = $true)][string]$NgxSdkPath,
    [string]$BuildDirectory,
    [string]$InstallDirectory,
    [string]$CMake = 'cmake',
    [switch]$SkipGpuSelfTest
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (!$BuildDirectory) { $BuildDirectory = Join-Path $repoRoot 'build-enhancement' }
$sdk = (Resolve-Path -LiteralPath $NgxSdkPath).Path
foreach ($header in @('nvsdk_ngx.h', 'nvsdk_ngx_defs_dlssg.h')) {
    if (!(Test-Path -LiteralPath (Join-Path $sdk "include/$header"))) {
        throw "Missing NVIDIA SDK header: include/$header"
    }
}
# The GUI may use MinGW; NGX's C++ parameter ABI requires an MSVC worker.
& $CMake -S (Join-Path $repoRoot 'gui/enhancement') -B $BuildDirectory `
    -G 'Visual Studio 17 2022' -A x64 "-DCHIAKI_NGX_SDK:PATH=$sdk" -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
if ($LASTEXITCODE) { throw 'Worker configuration failed' }
& $CMake --build $BuildDirectory --config Release --parallel
if ($LASTEXITCODE) { throw 'Worker build failed' }
& $CMake --build $BuildDirectory --config Release --target enhancement-model-test
if ($LASTEXITCODE) { throw 'Model test build failed' }
& (Join-Path $BuildDirectory 'Release/enhancement-model-test.exe')
if ($LASTEXITCODE) { throw 'Model tests failed' }
if ($SkipGpuSelfTest) {
    Write-Host '[RTX][Build] NVIDIA GPU self-test explicitly omitted on hosted CI; model and packaged loader checks still apply.'
} else {
    $worker = [Diagnostics.ProcessStartInfo]::new()
    $worker.FileName = (Resolve-Path -LiteralPath (Join-Path $BuildDirectory 'Release/nvngx.dll')).Path
    $worker.Arguments = '--self-test'
    $worker.UseShellExecute = $false
    $worker.CreateNoWindow = $true
    $worker.RedirectStandardError = $true
    $workerProcess = [Diagnostics.Process]::Start($worker)
    $diagnostic = $workerProcess.StandardError.ReadToEnd()
    $workerProcess.WaitForExit()
    Write-Output $diagnostic
    if ($workerProcess.ExitCode) { throw 'D3D12 shader tests failed' }
}
if ($InstallDirectory) {
    & $CMake --install $BuildDirectory --config Release --prefix $InstallDirectory
    if ($LASTEXITCODE) { throw 'Worker installation failed' }
}
