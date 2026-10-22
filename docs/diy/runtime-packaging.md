# Enhancement runtime sources and packaging

## NVIDIA runtime and SM86 distinction

`nvngx_dlssg.dll` is NVIDIA’s Frame Generation runtime. Official FG supports RTX 40 and 50 series; Multi Frame Generation is a separate capability. Chiaki-ngx currently requests 2× FG and has not validated native FG on every supported GPU.

The previously used outer DLL exactly matches the Git blob in [NVIDIA/DLSS, Windows x64 release](https://github.com/NVIDIA/DLSS/blob/9981c6350dc58c0ce21f024fd65e31e851c31689/lib/Windows_x86_64/rel/nvngx_dlssg.dll). The SM86 bundle’s [third-party notice](https://github.com/sdli1995/dlssg_for_sm86/blob/6dcfd54c51e7fc2eb4fe25e5bb16044809c647b5/THIRD_PARTY_NOTICES.txt) identifies its own copy as unmodified NVIDIA material. Both inspected files have valid NVIDIA Authenticode signatures; they differ despite equal version and size. The unofficial changes are in SM86’s proxy, backend and derived kernel pack, which hook runtime execution.

| Runtime 310.9.16.0 | SHA256 |
|---|---|
| NVIDIA GitHub x64 release | `8d59d49b36410dd6b62fef2503a5871d347415bd85d943fa0848563a95dd50c7` |
| SM86 bundle’s matching signed runtime | `cadee1aa6db3907babdc3285da2aebc4553d92b89c38ba3eae8abd8bba1bea72` |

The release bundles the matching FG runtime. Maintainer download sources: [NVIDIA SDK](https://github.com/NVIDIA/DLSS/tree/main/lib/Windows_x86_64/rel), or the independent [TechPowerUp FG mirror](https://www.techpowerup.com/download/nvidia-dlss-3-frame-generation-dll/). Match the release’s pinned file identity, not just its version string. TechPowerUp’s [DLSS SR page](https://www.techpowerup.com/download/nvidia-dlss-dll/) concerns `nvngx_dlss.dll`, which is unused here. Mirror files were not independently verified during this check.

`nvngx_dlssnr.dll` is a separate experimental Neural Rendering dependency. It is absent from the checked public NVIDIA SDK tree; no authorized public download was verified. The supplied test binary is unsigned, so its official provenance cannot be established from that file. Do not describe it as a public SDK download, rename the SR DLL to substitute for it, or distribute it based on these tests. Users need their own legitimately obtained compatible NR runtime. NR hardware support is separate from FG support.

## Optional unofficial RTX 20 / 30 route

[DLSSG for SM86](https://github.com/sdli1995/dlssg_for_sm86/) supplies the proxy/backend/kernel route for older GPUs. Use it at your own risk and follow the bundle’s terms. This does not establish older-card NR compatibility.

Use one matching stock FG runtime at `runtimes/nvngx_dlssg.dll`. The prebuilt SM86 INI selects `[Runtime] Mode=External`, `Path=../nvngx_dlssg.dll`, relative to `runtimes/sm86-proxy/`. Keep `version.dll`, `dlssg_sm86.ini`, `dlssg_sm86/backend/sm86_backend.dll` and `dlssg_sm86/kernels/fg_kernels.dll` in their relative layout. The nested runtime copy is unnecessary. See [upstream runtime configuration](https://github.com/sdli1995/dlssg_for_sm86/blob/6dcfd54c51e7fc2eb4fe25e5bb16044809c647b5/docs/INSTALL.en.md).

The checked upstream English README declares GPLv3, while its third-party notice and the supplied `LICENSE.md` state MIT; those statements conflict. The supplied notice is retained. NVIDIA runtimes and derived kernels have separate terms; the entire bundle must not be presented as uniformly MIT or GPL. The source project retains its existing Chiaki license and author notices.

## Release maintainers

Build the worker with `scripts/Build-EnhancementWorker.ps1`; it produces the project’s own `nvngx.dll` executable. Stage standard Qt/FFmpeg/libplacebo dependencies from the chosen toolchain. The GUI build does not download external model DLLs.

For a native-only FG package:

```powershell
./scripts/Install-EnhancementRuntimes.ps1 -Destination <app>/runtimes -FrameGenerationDll <official-nvngx_dlssg.dll>
```

For a package that also bundles the optional older-card SM86 route, supply `-Sm86ProxyDirectory <matching-bundle>` instead; the script selects that bundle’s matching stock runtime. `-NeuralRenderingDll <compatible-NR-runtime>` is optional and separate. Staging adjusts the SM86 INI, preserves required notices, excludes unused SR/XeSS/development files and hashes the resulting files. It never downloads dependencies or establishes redistribution rights.

The version-2 manifest checks only enabled features: NR needs its NR file; FG needs the stock FG file; SM86 files are required only when FG is requested and the adapter selects the SM86 route (consumer RTX 20/30). The native FG entry can pin both checked NVIDIA-signed variants; the SM86 package overrides that entry with its backend’s exact matching stock runtime. A same-version DLL from a different build is rejected for that route. Missing NR or SM86 files do not block the official FG route. Version-1 manifests retain their original all-files validation. Wrong hashes, unpinned files and paths escaping the runtime directory are rejected before starting the worker.

The prebuilt user ZIP must contain worker, configuration, matching manifest and required notices, with only the NR model DLL omitted. The stock `nvngx_dlssg.dll` is bundled once at `runtimes/`. The optional SM86 proxy/backend/kernel DLLs and their required notices are bundled; RTX 40/50 bypass them and use the native NVIDIA path. End users extract and run `chiaki.exe`; only those enabling NR supply their compatible `runtimes/nvngx_dlssnr.dll`. They do not run staging scripts. Keep one FG DLL. Do not include private local build metadata, source snapshots or handoff journals. GitHub Actions still needs explicit worker/runtime staging to reproduce an enhanced release.

## Local release candidate

Routine local builds use only **`release/ngx`**, with **`Launch-ReleaseCandidate.cmd`** at the project root. Preserve `release/original` as the reference build. Do not create dated release folders or ZIPs for each build.

Once the Release GUI and MSVC worker build trees are configured and a portable dependency/runtime tree is available, run PowerShell 7:

```powershell
./scripts/Build-ReleaseCandidate.ps1
```

The helper rebuilds `build-rc-gui` and `build-rc-worker`, stages the existing `release/ngx` dependencies, verifies runtime pins, installs freshly built GUI/worker files and replaces `release/ngx`. It derives CMake and the SDK license from the configured build caches. Build logs are in `build-local-release/logs`. Running copies are not terminated; close the app before replacing its folder. Temporary staging and rollback directories are removed after success. Existing local SM86 logs are retained.

For initial setup, supply `-InputDirectory <complete-staged-portable-app>`. Override `-GuiBuildDirectory`, `-WorkerBuildDirectory`, `-CMake` or `-NvidiaSdkLicense` when necessary.

For local NR testing, supply `-NeuralRenderingDllForTesting <compatible-nvngx_dlssnr.dll>` once. Its SHA256 must match the input manifest's neural pin. The helper installs it at `release/ngx/runtimes/nvngx_dlssnr.dll` and retains that matching file on later rebuilds. The local README/build metadata identify NR as included for testing. This does not change the public package's NR exclusion or provenance requirements. The entire release directory and runtime DLLs remain ignored by Git.

No archive is created by this helper. Use the public packaging step below, with `-ArchivePath` only when an archive is explicitly requested.

## Final prebuilt package

After building and staging the complete GUI, worker, standard dependencies and pinned runtime tree, use PowerShell 7 to create a fresh distributable directory:

```powershell
./scripts/Package-EnhancementRelease.ps1 -InputDirectory <staged-app> -DestinationDirectory <new-release-directory> -NvidiaSdkLicense <SDK>/LICENSE.txt
```

This packaging step bundles the FG DLL and SM86 proxy/backend/kernel files, keeps their legal notices and includes the NVIDIA SDK and AMD FSR 1 licenses. It excludes `nvngx_dlssnr.dll` (and the shorthand `dlssnr.dll`) anywhere in the copied tree, including stale copies, while retaining the compatible NR hash in the version-2 manifest. FG and SM86 file hashes are checked before packaging. Source inputs are unchanged. Local logs, crash dumps, debug symbols and source snapshots are excluded; private build metadata is not copied into the release manifest. Existing output paths are rejected rather than overwritten.

For NR support, stage a compatible NR reference first with `Install-EnhancementRuntimes.ps1` so its hash is known; the final packaging step omits that binary. A release with no NR pin still supports the bundled FG route, but cannot enable NR until a compatible manifest is supplied by the release maintainer. This is a maintainer step, not an extra user installation step.

Add `-ArchivePath <release.zip>` only when a ZIP is requested. The packaging helper does not build the GUI or download third-party dependencies. Existing GitHub Actions workflows have not been changed to run it automatically. All DLLs and release archives remain ignored by Git; they belong in release artifacts, not source commits.

See [enhancement diagnostics](enhancements.md#bug-reports) for logs to attach to bug reports.
