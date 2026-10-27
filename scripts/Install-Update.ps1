# Windows portable updater. Compatible with the Windows PowerShell shipped with Windows.
param(
    [Parameter(Mandatory=$true)][string]$ArchivePath,
    [Parameter(Mandatory=$true)][string]$InstallDirectory,
    [Parameter(Mandatory=$true)][int]$ParentPid,
    [Parameter(Mandatory=$true)][string]$ExpectedVersion,
    [Parameter(Mandatory=$true)][string]$ExpectedSha256,
    [Parameter(Mandatory=$true)][string]$StatePath,
    [Parameter(Mandatory=$true)][string]$LogPath
)
$ErrorActionPreference='Stop'
$install=[IO.Path]::GetFullPath($InstallDirectory).TrimEnd('\')
$archive=[IO.Path]::GetFullPath($ArchivePath)
$cache=[IO.Path]::GetDirectoryName($archive)
$stageRoot=$null; $previous=$null; $newProcess=$null; $moved=$false; $installed=$false; $readySent=$false
function WriteLog([string]$Message,[string]$Severity='INFO') {
    $safe=$Message.Replace($install,'<app>').Replace($cache,'<update-cache>')
    if($env:USERPROFILE) {$safe=$safe.Replace($env:USERPROFILE,'<user>')}
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($LogPath)) | Out-Null
    [IO.File]::AppendAllText($LogPath,([DateTime]::UtcNow.ToString('o')+' '+$Severity+' [Update][Installer] '+$safe+"`r`n"),[Text.UTF8Encoding]::new($false))
}
function State([string]$Value) {
    $json=@{state=$Value;version=$ExpectedVersion}|ConvertTo-Json -Compress
    $temp=$StatePath+'.next'
    [IO.File]::WriteAllText($temp,$json,[Text.UTF8Encoding]::new($false))
    Move-Item -LiteralPath $temp -Destination $StatePath -Force
}
function CanonicalVersion([string]$Value) {
    if($Value -notmatch '^v?(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:\.(0|[1-9][0-9]*))?(-[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$') {throw 'Invalid release version.'}
    $patch=if($Matches[3]){$Matches[3]}else{'0'}
    return $Matches[1]+'.'+$Matches[2]+'.'+$patch+$Matches[4]
}
function Contained([string]$Root,[string]$Relative) {
    $base=[IO.Path]::GetFullPath($Root).TrimEnd('\')
    $path=[IO.Path]::GetFullPath([IO.Path]::Combine($base,$Relative))
    if(!$path.StartsWith($base+'\',[StringComparison]::OrdinalIgnoreCase)) {throw 'Package path escapes its directory.'}
    return $path
}
function NoLinks([string]$Directory) {
    $items=@(Get-Item -LiteralPath $Directory -Force)+@(Get-ChildItem -LiteralPath $Directory -Recurse -Force)
    foreach($item in $items) {
        if($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {throw 'Update folders must not contain links.'}
    }
}
function CopyData([string]$Source,[string]$Destination) {
    if(!(Test-Path -LiteralPath $Source)) {return}
    if(Test-Path -LiteralPath $Source -PathType Leaf) {
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Destination))|Out-Null
        Copy-Item -LiteralPath $Source -Destination $Destination -Force;return
    }
    foreach($file in Get-ChildItem -LiteralPath $Source -Recurse -File) {
        $relative=$file.FullName.Substring($Source.TrimEnd('\').Length+1)
        $target=Contained $Destination $relative
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target))|Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $target -Force
    }
}
function RunningFromInstall {
    foreach($process in [Diagnostics.Process]::GetProcesses()) {
        if($process.ProcessName -notin @('chiaki','nvngx','nvngx.dll')) {continue}
        try {$path=$process.MainModule.FileName} catch {continue}
        if($path.StartsWith($install+'\',[StringComparison]::OrdinalIgnoreCase)) {return $true}
    }
    return $false
}
function CleanStage {
    if(!$stageRoot -or !(Test-Path -LiteralPath $stageRoot)) {return}
    $full=[IO.Path]::GetFullPath($stageRoot)
    $parent=[IO.Path]::GetDirectoryName($install)
    if([IO.Path]::GetDirectoryName($full) -ne $parent -or [IO.Path]::GetFileName($full) -notmatch '^\.chiaki-ngx-update-[0-9a-f]{32}$') {throw 'Refusing cleanup outside the update staging folder.'}
    NoLinks $full
    Remove-Item -LiteralPath $full -Recurse -Force
}
function PreserveUserData {
    foreach($data in @('Chiaki.ini','pl_render_params.ini','logs','captures','screenshots','runtimes/sm86-proxy/dlssg_sm86/logs')) {
        CopyData (Join-Path $install $data) (Join-Path $next $data)
    }
    $nr=Join-Path $install 'runtimes/nvngx_dlssnr.dll'
    if(Test-Path -LiteralPath $nr -PathType Leaf) {
        CopyData $nr (Join-Path $next 'runtimes/nvngx_dlssnr.dll')
        if((Get-FileHash -LiteralPath $nr -Algorithm SHA256).Hash -notin @($manifest.packages.neural.'nvngx_dlssnr.dll')) {
            WriteLog 'Preserved supplied NR runtime; this release does not list its hash. NR may require a compatible runtime.' 'WARN'
        }
    }
}
try {
    $version=CanonicalVersion $ExpectedVersion
    if($install -eq [IO.Path]::GetPathRoot($install).TrimEnd('\') -or !(Test-Path -LiteralPath (Join-Path $install 'chiaki.exe'))) {throw 'Not an existing Chiaki portable installation.'}
    if([IO.Path]::GetFullPath($StatePath) -ne (Join-Path $cache 'state.json')) {throw 'Unexpected update state path.'}
    if($ExpectedSha256 -notmatch '^[0-9a-fA-F]{64}$' -or (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $ExpectedSha256) {throw 'Package SHA256 mismatch.'}
    NoLinks $install
    $stageRoot=Join-Path ([IO.Path]::GetDirectoryName($install)) ('.chiaki-ngx-update-'+[Guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($stageRoot)|Out-Null
    $payload=Join-Path $stageRoot 'payload';[IO.Directory]::CreateDirectory($payload)|Out-Null
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip=[IO.Compression.ZipFile]::OpenRead($archive)
    try {
        if($zip.Entries.Count -gt 20000) {throw 'Too many archive entries.'}
        $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        [long]$total=0
        foreach($entry in $zip.Entries) {
            $name=$entry.FullName.Replace('/','\').TrimEnd('\')
            if(!$name -or $name -match '[<>:"|?*\x00-\x1f]' -or $name.StartsWith('\')) {throw 'Invalid archive path.'}
            foreach($part in $name.Split('\')) {
                if(!$part -or $part -in @('.','..') -or $part.EndsWith('.') -or $part.EndsWith(' ') -or $part -match '^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)') {throw 'Unsafe archive path component.'}
            }
            $null=Contained $payload $name
            if(!$seen.Add($name)) {throw 'Duplicate archive path.'}
            if((($entry.ExternalAttributes -shr 16) -band 0xf000) -eq 0xa000 -or ($entry.ExternalAttributes -band 0x400)) {throw 'Links are not supported in update archives.'}
            $total+=$entry.Length
            if($total -gt 2147483648 -or $entry.Length -gt 1073741824) {throw 'Extracted update exceeds its size limit.'}
        }
        foreach($entry in $zip.Entries) {
            $target=Contained $payload $entry.FullName.Replace('/','\').TrimEnd('\')
            if($entry.FullName.EndsWith('/') -or $entry.FullName.EndsWith('\')) {[IO.Directory]::CreateDirectory($target)|Out-Null;continue}
            [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target))|Out-Null
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$target,$false)
        }
    } finally {$zip.Dispose()}
    $next=$payload
    if(!(Test-Path -LiteralPath (Join-Path $next 'chiaki.exe'))) {
        $children=@(Get-ChildItem -LiteralPath $payload -Force)
        if($children.Count -ne 1 -or !$children[0].PSIsContainer) {throw 'Archive has no portable application root.'}
        $next=$children[0].FullName
    }
    foreach($required in @('chiaki.exe','enhancement/nvngx.dll','runtimes/runtime-manifest.json','build-info.json','updates/Install-Update.ps1')) {
        if(!(Test-Path -LiteralPath (Join-Path $next $required) -PathType Leaf)) {throw "Incomplete update: $required"}
    }
    $identity=Get-Content -LiteralPath (Join-Path $next 'build-info.json') -Raw|ConvertFrom-Json
    if((CanonicalVersion $identity.app_version) -ne $version) {throw 'Package metadata version does not match the release.'}
    $exe=Join-Path $next 'chiaki.exe'
    $product=[Diagnostics.FileVersionInfo]::GetVersionInfo($exe).ProductVersion
    if((CanonicalVersion $product) -ne $version) {throw 'Executable version does not match the release.'}
    if((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -ne $identity.gui_sha256 -or
       (Get-FileHash -LiteralPath (Join-Path $next 'enhancement/nvngx.dll') -Algorithm SHA256).Hash -ne $identity.worker_sha256) {throw 'Packaged executable identity mismatch.'}
    $runtimeRoot=Join-Path $next 'runtimes'
    $manifest=Get-Content -LiteralPath (Join-Path $runtimeRoot 'runtime-manifest.json') -Raw|ConvertFrom-Json
    if($manifest.schema -ne 2) {throw 'Unsupported runtime manifest.'}
    foreach($group in @('frame_generation','sm86')) {
        $package=$manifest.packages.$group
        if(!$package -or !$package.PSObject.Properties.Count) {throw "Missing runtime package: $group"}
        foreach($pin in $package.PSObject.Properties) {
            $path=Contained $runtimeRoot $pin.Name
            if(!(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -notin @($pin.Value)) {throw "Runtime validation failed: $($pin.Name)"}
        }
    }
    PreserveUserData
    # The ready handshake keeps the current app open if preparation fails.
    WriteLog "Package verified and prepared; version=$ExpectedVersion"
    if(Test-Path -LiteralPath (Join-Path $cache 'cancel')) {throw 'Update canceled before restart.'}
    try {$parentProcess=[Diagnostics.Process]::GetProcessById($ParentPid)} catch {$parentProcess=$null}
    if($parentProcess -and $parentProcess.MainModule.FileName -ne (Join-Path $install 'chiaki.exe')) {throw 'Update parent is not this application.'}
    State 'ready';$readySent=$true
    if($parentProcess) {
        $deadline=[Diagnostics.Stopwatch]::StartNew()
        while(!$parentProcess.WaitForExit(100)) {
            if(Test-Path -LiteralPath (Join-Path $cache 'cancel')) {throw 'Update canceled before restart.'}
            if($deadline.Elapsed.TotalSeconds -gt 60) {throw 'Current application did not exit; installation was left unchanged.'}
        }
    }
    $wait=[Diagnostics.Stopwatch]::StartNew()
    while(RunningFromInstall) {if($wait.Elapsed.TotalSeconds -gt 30) {throw 'Another application instance is still using the installation.'};Start-Sleep -Milliseconds 250}
    if(Test-Path -LiteralPath (Join-Path $cache 'cancel')) {throw 'Update canceled before replacement.'}
    NoLinks $install
    PreserveUserData # Capture INI saves and log flushes completed during shutdown.
    $previous=Join-Path $stageRoot 'previous'
    # Exact absolute app/staging paths were checked above, including every reparse point.
    [IO.Directory]::Move($install,$previous);$moved=$true
    try {[IO.Directory]::Move($next,$install);$installed=$true} catch {[IO.Directory]::Move($previous,$install);$moved=$false;throw}
    $health=Join-Path $cache 'health.json'
    if(Test-Path -LiteralPath $health) {Remove-Item -LiteralPath $health -Force}
    $env:CHIAKI_UPDATE_READY_PATH=$health
    $newProcess=Start-Process -FilePath (Join-Path $install 'chiaki.exe') -WorkingDirectory $install -PassThru
    Remove-Item Env:CHIAKI_UPDATE_READY_PATH
    $wait.Restart();$healthy=$false
    while($wait.Elapsed.TotalSeconds -lt 30) {
        if(Test-Path -LiteralPath $health) {
            try {$healthState=Get-Content -LiteralPath $health -Raw|ConvertFrom-Json
                $healthy=$healthState.state -eq 'started' -and (CanonicalVersion $healthState.version) -eq $version
            } catch {}
            if($healthy) {break}
        }
        if($newProcess.HasExited) {break}
        Start-Sleep -Milliseconds 100
    }
    if(!$healthy) {throw 'New build did not complete startup; restoring the previous build.'}
    Start-Sleep -Milliseconds 1000
    if($newProcess.HasExited -and $newProcess.ExitCode -ne 0) {throw 'New build failed immediately after startup.'}
    WriteLog "Update installed; version=$ExpectedVersion"
    State 'installed'
    CleanStage
    Remove-Item -LiteralPath $archive -Force
    exit 0
} catch {
    $problem=$_.Exception.Message
    Remove-Item Env:CHIAKI_UPDATE_READY_PATH -ErrorAction SilentlyContinue
    try {
        if($moved -and (Test-Path -LiteralPath $previous)) {
            # Only the new process created by this updater can be stopped during rollback.
            if($newProcess -and !$newProcess.HasExited) {$newProcess.Kill();$newProcess.WaitForExit()}
            if($installed -and (Test-Path -LiteralPath $install)) {
                NoLinks $install
                $failed=Join-Path $stageRoot 'failed'
                [IO.Directory]::Move($install,$failed)
            }
            [IO.Directory]::Move($previous,$install);$moved=$false
            WriteLog 'Previous installation restored.' 'WARN'
        }
        WriteLog $problem 'ERROR'
        State 'error'
        # Preparation errors leave the original process running. After an attempted
        # restart, return the working original app instead of leaving the user without it.
        if($readySent -and !(RunningFromInstall)) {Start-Process -FilePath (Join-Path $install 'chiaki.exe') -WorkingDirectory $install}
        if(!$moved) {CleanStage}
    } catch {try {WriteLog ('Recovery requires manual attention: '+$_.Exception.Message) 'ERROR'} catch {}}
    exit 1
}
