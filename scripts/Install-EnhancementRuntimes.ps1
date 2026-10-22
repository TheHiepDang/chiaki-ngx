param(
    [Parameter(Mandatory = $true)][string]$Destination,
    [string]$NeuralRenderingDll,
    [string]$FrameGenerationDll,
    [string]$Sm86ProxyDirectory
)
$ErrorActionPreference = 'Stop'
# Validate the supplied model pair before changing the destination.
if ($Sm86ProxyDirectory) {
    $proxySource = (Resolve-Path -LiteralPath $Sm86ProxyDirectory).Path
    $stockRuntime = Join-Path $proxySource 'dlssg_sm86/runtime/nvngx_dlssg.dll'
    foreach ($required in @('version.dll','dlssg_sm86.ini','dlssg_sm86/backend/sm86_backend.dll',
            'dlssg_sm86/kernels/fg_kernels.dll','dlssg_sm86/runtime/nvngx_dlssg.dll')) {
        if (!(Test-Path -LiteralPath (Join-Path $proxySource $required) -PathType Leaf)) {
            throw "Incomplete proxy: $required"
        }
    }
    if ($FrameGenerationDll -and
        (Get-FileHash -LiteralPath $FrameGenerationDll -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath $stockRuntime -Algorithm SHA256).Hash) {
        throw 'SM86 needs its matching stock runtime. Omit -FrameGenerationDll to use the supplied proxy runtime.'
    }
    $FrameGenerationDll = $stockRuntime
}
$destinationPath = [IO.Path]::GetFullPath($Destination)
New-Item -ItemType Directory -Path $destinationPath -Force | Out-Null
if ($NeuralRenderingDll) {
    Copy-Item -LiteralPath (Resolve-Path -LiteralPath $NeuralRenderingDll).Path `
        -Destination (Join-Path $destinationPath 'nvngx_dlssnr.dll') -Force
}
if ($FrameGenerationDll) {
    Copy-Item -LiteralPath (Resolve-Path -LiteralPath $FrameGenerationDll).Path `
        -Destination (Join-Path $destinationPath 'nvngx_dlssg.dll') -Force
}
if ($Sm86ProxyDirectory) {
    $proxySource = (Resolve-Path -LiteralPath $Sm86ProxyDirectory).Path
    $proxyDestination = Join-Path $destinationPath 'sm86-proxy'
    if ($proxyDestination.StartsWith($proxySource.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) `
        -or $proxyDestination.Equals($proxySource, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The proxy destination must be separate from its source.'
    }
    $proxyRuntimeFiles = @('version.dll','dlssg_sm86.ini','dlssg_sm86/backend/sm86_backend.dll',
        'dlssg_sm86/kernels/fg_kernels.dll')
    foreach ($required in $proxyRuntimeFiles) {
        if (!(Test-Path -LiteralPath (Join-Path $proxySource $required))) { throw "Incomplete proxy: $required" }
    }
    foreach ($file in Get-ChildItem -LiteralPath $proxySource -File -Recurse) {
        $relative = [IO.Path]::GetRelativePath($proxySource, $file.FullName).Replace('\','/')
        $legalNotice = $relative -match '(^|/)(LICENSES?|COPYING|NOTICE|THIRD_PARTY_NOTICES|PATENTS?|COPYRIGHT|AUTHORS)([^/]*$|/)'
        if ($relative -notin $proxyRuntimeFiles -and !$legalNotice) { continue }
        $target = Join-Path $proxyDestination $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $target -Force
    }
    # One stock DLL serves both the worker and the SM86 backend. Preserve vendor
    # backend/kernel paths; External paths are relative to the proxy directory.
    $iniPath = Join-Path $proxyDestination 'dlssg_sm86.ini'
    $iniText = [IO.File]::ReadAllText($iniPath)
    $runtimeSection = [regex]::new('(?ms)^\[Runtime\][^\r\n]*\r?\n.*?(?=^\[|\z)')
    $sectionText = "[Runtime]`r`nMode=External`r`nPath=../nvngx_dlssg.dll`r`n`r`n"
    if ($runtimeSection.IsMatch($iniText)) {
        $iniText = $runtimeSection.Replace($iniText, $sectionText, 1)
    } else { $iniText += "`r`n" + $sectionText }
    [IO.File]::WriteAllText($iniPath, $iniText, [Text.UTF8Encoding]::new($false))
    $oldRuntime = Join-Path $proxyDestination 'dlssg_sm86/runtime/nvngx_dlssg.dll'
    if (Test-Path -LiteralPath $oldRuntime -PathType Leaf) { Remove-Item -LiteralPath $oldRuntime }
}
# Remove only known unused libraries from the destination, never from the supplied source.
foreach ($unused in @('sm86-proxy/dlssg_sm86/sr/nvngx_dlss.dll',
        'sm86-proxy/dlssg_sm86/sr/sr_kernels.dll',
        'sm86-proxy/dlssg_sm86/xefg/xefg_kernels.dll')) {
    $unusedPath = [IO.Path]::GetFullPath((Join-Path $destinationPath $unused))
    if (!$unusedPath.StartsWith($destinationPath.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) { throw 'Unused runtime path escaped its destination.' }
    if (Test-Path -LiteralPath $unusedPath -PathType Leaf) { Remove-Item -LiteralPath $unusedPath }
}
$files = [ordered]@{}
foreach ($file in Get-ChildItem -LiteralPath $destinationPath -Recurse -File | Sort-Object FullName) {
    $relative = [IO.Path]::GetRelativePath($destinationPath, $file.FullName).Replace('\','/')
    if ($relative -match '(^|/)(logs|capture)/' -or $relative -eq 'runtime-manifest.json') { continue }
    $files[$relative] = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
}
# Pin enabled features independently. The official NVIDIA x64 release variant
# below was checked against NVIDIA/DLSS commit 9981c6350dc58c0ce21f024fd65e31e851c31689.
# A prebuilt manifest can retain optional SM86/NR pins without requiring users
# of the official FG route to supply those separate packages.
$packages = [ordered]@{}
if ($files.Contains('nvngx_dlssnr.dll')) {
    $packages['neural'] = [ordered]@{ 'nvngx_dlssnr.dll' = $files['nvngx_dlssnr.dll'] }
}
if ($files.Contains('nvngx_dlssg.dll')) {
    $pins = @($files['nvngx_dlssg.dll'],
        '8d59d49b36410dd6b62fef2503a5871d347415bd85d943fa0848563a95dd50c7') | Select-Object -Unique
    $packages['frame_generation'] = [ordered]@{ 'nvngx_dlssg.dll' = @($pins) }
}
$proxyFiles = [ordered]@{}
foreach ($entry in $files.GetEnumerator()) {
    if ($entry.Key.StartsWith('sm86-proxy/')) { $proxyFiles[$entry.Key] = $entry.Value }
}
if ($proxyFiles.Count) {
    # Older-GPU backend/kernel RVAs require this bundle's exact stock runtime.
    $proxyFiles['nvngx_dlssg.dll'] = $files['nvngx_dlssg.dll']
    $packages['sm86'] = $proxyFiles
}
[ordered]@{ schema = 2; packages = $packages } | ConvertTo-Json -Depth 7 | `
    Set-Content -LiteralPath (Join-Path $destinationPath 'runtime-manifest.json') -Encoding utf8
Write-Output "Optional runtime package installed and pinned: $destinationPath"
