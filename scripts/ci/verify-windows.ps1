#Requires -Version 7.0
param([Parameter(Mandatory)][string]$Directory, [string]$Version, [switch]$BaselineOnly)
$ErrorActionPreference = 'Stop'
$directoryPath = (Resolve-Path -LiteralPath $Directory).Path
foreach ($required in @('chiaki.exe','qt.conf','platforms/qwindows.dll','platforms/qoffscreen.dll','qml/QtQuick','qml/QtQuick/Controls','qml/QtQuick/Layouts')) {
    if (!(Test-Path -LiteralPath (Join-Path $directoryPath $required))) { throw "[App][Package] Missing $required" }
}
foreach ($pattern in @('Qt6Core.dll','Qt6Gui.dll','Qt6Qml.dll','Qt6Quick.dll','*SDL2*.dll','*SDL3*.dll','avcodec-*.dll','avutil-*.dll','*placebo*.dll','*vulkan*.dll')) {
    if (!(Get-ChildItem -LiteralPath $directoryPath -Filter $pattern -File)) { throw "[App][Package] Missing runtime $pattern" }
}
& python (Join-Path $PSScriptRoot 'release.py') windows-deps --folder $directoryPath
if ($LASTEXITCODE) { throw '[App][Package] Native dependency verification failed.' }
if (!$BaselineOnly) {
    foreach ($required in @('enhancement/nvngx.dll','runtimes/runtime-manifest.json','build-info.json','updates/Install-Update.ps1')) {
        if (!(Test-Path -LiteralPath (Join-Path $directoryPath $required))) { throw "[App][Package] Missing $required" }
    }
    $info = Get-Content (Join-Path $directoryPath 'build-info.json') -Raw | ConvertFrom-Json
    foreach ($item in @(@('chiaki.exe','gui_sha256'),@('enhancement/nvngx.dll','worker_sha256'))) {
        if ((Get-FileHash (Join-Path $directoryPath $item[0])).Hash.ToLowerInvariant() -ne $info.($item[1])) { throw '[App][Package] Executable identity mismatch.' }
    }
    if ($info.app_version -ne $Version) { throw '[App][Package] Version metadata mismatch.' }
    if (Get-ChildItem -LiteralPath $directoryPath -Recurse -File | Where-Object {$_.Name -in @('nvngx_dlssnr.dll','dlssnr.dll')}) { throw '[RTX][Package] Public package contains NR.' }
}
function RunClean([string]$Exe, [string]$Arguments) {
    $start = [Diagnostics.ProcessStartInfo]::new($Exe, $Arguments)
    $start.WorkingDirectory = $directoryPath
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.RedirectStandardInput = $true
    # Deliberately exclude installed Qt/MSYS2/FFmpeg paths from the child's environment.
    $start.Environment['PATH'] = "$env:SystemRoot/System32;$env:SystemRoot"
    foreach ($key in @('QT_PLUGIN_PATH','QT_QPA_PLATFORM_PLUGIN_PATH','QML2_IMPORT_PATH','QML_IMPORT_PATH')) { $start.Environment.Remove($key) | Out-Null }
    $start.Environment['QT_QPA_PLATFORM'] = 'offscreen'
    $start.Environment['QT_QUICK_BACKEND'] = 'software'
    $start.Environment['SDL_AUDIODRIVER'] = 'dummy'
    $start.Environment['QTWEBENGINE_DISABLE_SANDBOX'] = '1'
    $start.Environment['QT_PLUGIN_PATH'] = $directoryPath
    $start.Environment['QT_QPA_PLATFORM_PLUGIN_PATH'] = Join-Path $directoryPath 'platforms'
    $start.Environment['QML2_IMPORT_PATH'] = Join-Path $directoryPath 'qml'
    $start.Environment['CHIAKI_DIAGNOSTIC_LOG_DIR'] = Join-Path $env:RUNNER_TEMP 'chiaki-package-logs'
    $process = [Diagnostics.Process]::Start($start)
    $process.StandardInput.Close()
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    if (!$process.WaitForExit(60000)) { $process.Kill(); throw '[App][Package] Startup check timed out.' }
    $result = $stdout.Result + $stderr.Result
    Write-Host $result
    if ($process.ExitCode) { throw "[App][Package] Startup check failed, exit=$($process.ExitCode)" }
    return $result
}
$executable = Join-Path $directoryPath 'chiaki.exe'
$actual = [Diagnostics.FileVersionInfo]::GetVersionInfo($executable).ProductVersion
if ($Version -and $actual -ne $Version) { throw '[App][Package] Executable resource version mismatch.' }
$runtimeVersion = RunClean $executable '--version'
if (!$runtimeVersion.Contains($actual)) { throw '[App][Package] Runtime version differs from resource version.' }
RunClean $executable '--package-smoke-test' | Out-Null
if (!$BaselineOnly) { RunClean (Join-Path $directoryPath 'enhancement/nvngx.dll') '' | Out-Null }
Write-Host '[App][Package] Clean-path native startup and Qt/QML passed. RTX hardware testing is separate.'
