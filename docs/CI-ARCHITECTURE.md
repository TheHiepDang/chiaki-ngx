# CI architecture and upstream audit

Scope: Windows x64 portable ZIP and installer EXE, plus Linux x86_64 AppImage.
All three verified artifacts are required for publication. Linux ARM64 remains manual-only.

## Upstream reference

Inspected `chiaki-ng/chiaki-ng` main at
[`c8bea2642d0e95402f373af55584a3b98cd89417`](https://github.com/chiaki-ng/chiaki-ng/tree/c8bea2642d0e95402f373af55584a3b98cd89417)
before this repair. The local `upstream` remote has push URL `DISABLED`; nothing was merged
or rebased. The comparison covers the upstream Windows MSYS2, AppImage and release workflows,
`build-ffmpeg.sh`, `build-appimage.sh` and `deploy-windows-msys2.sh`.

| Mechanism | Inherited/useful | Fork handling |
|---|---|---|
| Windows CMake/Ninja, MSYS2 deployment, windeployqt, Inno Setup | Retained build and packaging scripts | Checksum-locked CLANG64 replaces obsolete MINGW64 recipe; strict post-Qt dependency audit and explicit offscreen plugin staging |
| Linux Qt builder container, FFmpeg/SDL/libplacebo/hidapi builds, core tests, linuxdeploy/Qt plugin | Retained existing AppImage recipe | Pin image digest, source revisions and deployment downloads; test extracted package outside builder |
| Upstream dependency downloads | Several use latest/continuous URLs and unverified moving refs | Fixed hashes/commits, fail closed; FFmpeg uses pinned GitHub commit instead of refused git.ffmpeg.org endpoint |
| Upstream release distribution | ZIP/installer/AppImage formats remain useful | Replace early public release, PAT and allowUpdates with built-in token, draft-first publication and byte/digest verification |
| Fork RTX additions | Absent upstream | Build MSVC worker separately from MSYS GUI, deploy matched runtime manifest/FG support, retain license notices; user supplies NR DLL |
| Fork updater/versioning | Requires canonical Windows ZIP names and GitHub asset digests | Shared tag stamp including prerelease suffix, recorded version overlay, installer/helper and provenance/checksum checks |

The original Windows error was not a bad mirror: the current official MINGW64 catalogs
omit hidapi while CLANG64 provides it. Upstream's recipe is outdated for the current package
catalog. The shared pinned GitHub FFmpeg path fixes the inherited refused upstream endpoint.

## Minimal production workflow graph

| Entry point | Builds | Permissions/publication |
|---|---|---|
| `build-pr.yaml` | Windows + Linux, checked-out PR merge commit | Read-only; never calls publisher |
| `build-weekly.yaml` | Same jobs | Read-only, Actions artifacts only |
| `build-release.yaml` manual dispatch | Windows, Linux or both | Read-only validation path; no release |
| `build-release.yaml` version tag | Both, exact tagged commit on remote main | Publisher alone gets contents write after all three packages pass |

All entry points reuse `build-release-assets.yml`. Older platform/compatibility workflows
remain explicit manual tools, outside the required release graph. Linux ARM64 is not an
automatic PR job. Native application source for other platforms remains intact.

## Windows installed-package distinction

Portable files are checked as x64, including every EXE, DLL, plugin and worker. After Inno's
silent test installation, every portable file must be byte-identical to its installed copy.
Only the installed root `unins000.exe` is permitted to be Inno's PE32 auxiliary executable.
Its normal and delay imports are still parsed and must resolve to 32-bit Windows system
libraries (`SysWOW64`), not the application's x64 DLLs. The exception is explicit and unavailable
to portable verification, nested files, plugins or similarly named executables. Installer,
version, runtime hashes, updater checks and clean-PATH startup checks remain required.

## Linux runtime boundary and complete preflight

`linux-runtime.json` records the exact host SONAMEs deliberately excluded by the pinned
linuxdeploy deployment, plus the upstream `libva*`/`libvulkan*` exclusions. It groups the host
C/C++ runtime, desktop services, window/font/audio interfaces and graphics dispatch/acceleration
interfaces. Ubuntu CI installs that complete package list rather than discovering one library
per startup failure. This includes both `libOpenGL.so.0` and `libva-drm.so.2` from the failed runs.
SteamOS supplies equivalent interfaces through its system image and graphics stack; do not
try to install Debian package names on SteamOS. This is a desktop baseline, not a headless
minimal-server promise. Hardware acceptance on SteamOS is still required.

Everything outside the explicit host boundary must be bundled, including Qt, Qt plugins,
SDL, FFmpeg, libplacebo and their non-system dependencies. Mesa DRI/VA drivers and NVIDIA/CUDA
vendor drivers must stay on the host; CI never copies its graphics driver into the package.
Upstream excludes driver interfaces intentionally to avoid mismatching the user's graphics stack.

`audit-appimage.py` enumerates every ELF (launcher/targets, executables, helpers, libraries and
plugins), checks x86_64 format, records DT_NEEDED and RPATH/RUNPATH, and runs the host loader
with only the package's `usr/lib` added. It checks all transitive resolutions, aggregates the
entire unresolved set and retains loader errors, including missing symbol versions. Each
bundled ELF's direct edges must resolve either inside the package or to the explicit host
boundary: an installed host Qt cannot mask incomplete packaging. Broken/escaping symlinks
and packaged vendor GPU drivers fail validation. Only after this succeeds does the launcher
run version and Qt/QML startup checks. No feature or test is disabled to satisfy the baseline.

The always-retained `diagnostics-linux` artifact includes the exact AppImage and
`appimage-elf-audit.json`; inspect or rerun its audit without rebuilding. Successful validation
proves the pinned builder output can load on Ubuntu 24.04, not that it supports every older
distribution or every GPU. Loader/ABI failures on another supported desktop should be reproduced
against that exact package and recorded host interfaces before changing the dependency policy.
