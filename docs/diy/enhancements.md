# Windows video enhancements

## Controls

Open the live RTX settings panel from the stream menu or Ctrl+Shift+F (rebind under Settings → Keys). RTX settings and Eye Candy are tabs. The RTX panel, its stream-menu buttons and shortcut are available only on Windows with the active Vulkan adapter identified as NVIDIA RTX. Missing NR/FG DLLs do not hide settings on eligible hardware; feature failures still report their own reason. The saved shortcut key remains compatible. General Video renderer options and its Eye Candy tab remain available on other GPUs. Tab/Shift+Tab and header arrows move focus; Ctrl+Tab or controller L1/R1 switches tabs. The dialog follows its content height and scrolls only when the window is too short.

- Neural Rendering (NR) is experimental and off by default. Resolution changes apply once, three seconds after the last slider movement. RTX controls lock with an applying indicator until processed output or a reported failure.
- Choose 1, 2 or 3 NR passes. One pass preserves the default and pass-1 legacy settings. Extra passes follow **all** pass-1 settings until Customize each pass is checked. Configure pass then selects an independent editor for each pass. On first enabling customization, missing custom fields copy pass 1's applied values; existing custom fields are retained. Turning customization off preserves saved custom values, returns the editor to pass 1, and discards pending custom-pass resolution drafts. Pending pass-1 resolution still uses its original three-second timer. Re-enabling customization restores saved edits.
- Each pass offers NR preset Default (style 0), Natural (1) or Cinematic (2), Intensity 0–1, Local Structure 0–2, Local Tone 0–2, Skin Structure −1–2.5 and Automatic Mask. Strength sliders step by 0.05. The three presets select the model, independently of the strength values; they do not reset the sliders. Skin Structure requires Automatic Mask; −1 is Off. Mask defaults off, Skin Structure defaults Off, Intensity and Local Structure/Tone default 1 and the model defaults to Default. High strength and repeated local tone can visibly compound across passes; no hidden tone suppression is applied to inherited settings.
- Resolution is 50–100% of the original displayed video area, retaining the existing NR size cap. FSR reconstruction and FSR sharpness (0–100%) apply only to reduced passes. Inactive/custom pass settings remain profile-local. Selecting an editor does not reconfigure rendering. Pending resolutions remain attached to their original pass and commit together three seconds after the last adjustment. Switching profiles cancels pending edits.
- The ↺ button next to each NR slider resets that value in the selected pass: Intensity 1, Local Structure 1, Local Tone 1, Skin Structure −1/Off, resolution 65%, FSR sharpness 20%. Other sliders, model/mask choices and custom passes are preserved. In shared mode, extra passes follow the reset pass-1 value as usual. Resolution reset queues the same three-second delay; strength/sharpness resets apply live. Buttons support controller/keyboard confirmation, scroll into view when focused in a short window, and have accessible labels/tooltips. Skin reset remains available while Automatic Mask is off, so its saved value can still be cleared.
- FSR 1 reconstructs reduced NR output and offers sharpening. It is the current upscaler; DLSS Super Resolution is not integrated.
- Frame generation (FG) is an independent experimental 2× mode, with NR on or off. Generated presentation may be skipped when its cost would reduce real-frame cadence.
- Eye Candy reuses brightness, contrast, saturation and gamma. Each slider has an individual reset. Exposure and Dark Area range from −1 to +1 EV and affect final SDR video, including native resolution and screenshots. HDR bypasses those two adjustments.
- F4 saves a PNG screenshot of the processed SDR video before Chiaki overlays; the shortcut is rebindable.

Controls persist in the current Chiaki settings profile. Submitted real/generated FPS are application submission counts, not console rendering or monitor scanout measurements.

## Runtime architecture

The GUI processes decoded video with Vulkan/libplacebo and exchanges shared textures/fences with the MSVC D3D12 worker. The worker is an executable intentionally named `enhancement/nvngx.dll`; it is started through a no-shell process API. Bundled FG/proxy/backend/kernel DLLs live under `runtimes/` beside `chiaki.exe`. Only the optional NR model DLL is supplied by the user. See [runtime packaging](runtime-packaging.md) for sources, file identity, optional package checks and installation paths.

Processing is native-size SDR prepass → one to three sequential NR/reconstruction stages → optional FG once → final color adjustment → presentation → application overlays. Each stage reduces the preceding stage's native-size result to its own NR resolution and reconstructs against that same stage input, preserving earlier edits. NR stages own separate feature handles, temporal history and intermediate textures while sharing one initialized NR runtime. Optical flow and scene-cut detection run once per decoded frame; history resets apply to every pass. Chain creation/evaluation failures name the failed pass and restore the ordinary fallback rather than presenting a partial chain as successful. GPU NR/reconstruction timings sum all active passes. Optical flow is estimated from video; game-engine motion/depth and separated game HUD buffers are unavailable. Worker/protocol/runtime failures restore ordinary playback and report their reason. NR evaluation parameters are passed with each command and cached per stage: unchanged model/strength/mask values are not rewritten each frame. Changing any pass setting resets that pass and downstream temporal histories on the next evaluated frame. Model/strength/mask and FSR/sharpness edits do not recreate textures or NGX features; changing the active pass count or an active pass resolution does. The applying overlay remains until a complete chain output or failure. GUI/worker IPC is version 5; deploy the rebuilt pair together.

## Build and checks

Build the ordinary GUI using the platform’s existing build guide. On Windows, build the separate worker with MSVC and a separately supplied NGX SDK:

```powershell
./scripts/Build-EnhancementWorker.ps1 -NgxSdkPath <SDK-directory> -BuildDirectory <worker-build> -InstallDirectory <staging-directory>
```

The GUI may use MinGW; the worker uses MSVC for the NGX C++ ABI. Preserve the deployed Qt/QML/FFmpeg/libplacebo dependencies. GitHub Actions currently does not stage the enhancement worker or external runtime package automatically.

Focused checks live in `gui/enhancement/tests`: model math, QML interaction, runtime manifests, diagnostic redaction/rotation/crash capture and GPU tone output. Bridge and full-window synthetic diagnostics validate shared resources and output identity; they do not establish physical-controller behavior, visual quality, input latency or long-session stability.

Developer overrides `CHIAKI_ENHANCEMENT_WORKER` and `CHIAKI_ENHANCEMENT_RUNTIME_DIR` select alternate absolute paths. `CHIAKI_RENDER_DIAGNOSTIC=off|nr|fg|nrfg` and an isolated `CHIAKI_RENDER_DIAGNOSTIC_ROOT` enable synthetic checks without a console session. `CHIAKI_ENHANCEMENT_TRACE=1` enables verbose NGX parameter diagnostics.

## Bug reports

Open Settings → General → Log Directory → Open. Attach the matching `chiaki_ngx_*.log`, any `.previous.log`, the session log and crash text/dump if present. For SM86 FG, also include the matching `runtimes/sm86-proxy/dlssg_sm86/logs/loader_*.jsonl` and `backend_*.jsonl`. Reporting instructions appear in application logs. Logs redact common credentials; crash dumps can contain private memory and should be reviewed before sharing. Nothing is uploaded automatically.

## Diagnostic tags

RTX bridge logs use `[RTX]` with contextual subtags (runtime validation, worker, NR, FG, settings, timing and recovery). Eye Candy configuration/SDR-HDR application state uses `[EyeCandy]`; image saves use `[Screenshot]`; startup/shutdown use `[App]`. Every persisted line retains a timestamp, severity and subsystem identity. Worker stderr is buffered across partial UTF-8/line chunks, tagged and redacted before storage; its stdout remains the binary IPC channel. Oversized worker lines are discarded with an explicit bounded-buffer message. Upstream SM86 JSONL files keep their original format.

The persistent log header and README list the main/session/rotated/crash and optional SM86 reports to attach. Logs are flushed and rotated; crash capture is best-effort. Controlled GUI and worker exception checks verify tagged reports/minidumps without crashing a user's application session. See `AGENTS.md` and the contributor guide for the ongoing logging convention.

Multi-pass worker diagnostics identify `[RTX][NR][Pass1]`, `[RTX][NR][Pass2]` and `[RTX][NR][Pass3]` creation, first evaluation and bounded timing samples. Include pass count, Customize each pass state, model, strengths, mask, resolution, FSR and sharpness in a bug report. Per-pass controls logs report the effective values, and downstream history resets identify upstream settings changes. Values are logged on change, not on every stable frame. Protocol and settings validation reject non-finite strengths, out-of-range values and unsupported model/mask identifiers. Synthetic GPU checks verify distinct outputs for the three models, live independent controls without worker restarts and downstream history resets; visual quality and skin detection remain content-dependent.


## App versions and updates

`cmake/Version.txt` is the app version source, currently `0.1`; CMake and Windows ProductVersion use the same value. Existing CI archive names read this file too. The home footer shows the channel, an update/check action and the current version. Release is the default; Pre-release includes stable and preview builds. The channel is an app-wide preference. Startup checks are asynchronous, and channel changes discard obsolete replies. Offline/rate-limit failures offer retry. Changing channel controls future updates; an older release is never installed automatically.

The checker reads this fork's GitHub Releases API, ignores drafts, compares numeric and prerelease versions, and recognizes the legacy `pre-release` tag/name as version 0.1. A Windows x64 portable asset must have the expected repository URL, name, byte count and GitHub SHA256 digest. A release without a supported verified asset can be viewed on GitHub instead. Download/install begins only after user confirmation and is blocked during streaming.

The detached Windows PowerShell helper validates/extracts the ZIP before asking the old process to exit. It rejects traversal, duplicate paths, links, excessive unpacked sizes, version/identity mismatches and invalid bundled runtimes. It preserves portable INI files, logs/captures and supplied NR, including shutdown saves/flushes. A startup handshake confirms the new build; a failed start restores and restarts the previous installation. No elevation or background forced update is used. Installer details are in `chiaki_update_*.log` beside the main application logs; include that file when reporting an update failure. A preserved NR DLL still must match the new release's runtime manifest to enable NR.

Local build outputs live under `build/` (GUI, worker, focused tests, staging and release logs). Rebuild the current test candidate with `scripts/Build-ReleaseCandidate.ps1`; keep only `release/ngx` plus the user's `release/original` reference. Public ZIP packaging remains the existing `scripts/Package-EnhancementRelease.ps1`, used only when an archive is requested. No release is published automatically.

For a future public update, build with the intended version in `cmake/Version.txt`, publish a matching tag such as `v0.2` or `v0.2-rc.1`, and mark preview releases as prereleases on GitHub. Supported Windows x64 asset names are `chiaki-ngx.zip` or `chiaki-ngx-windows-x64-v<version>.zip`. Package the complete portable app with the existing helper so executable versions, metadata, updater and runtime notices stay together. The checker uses GitHub’s asset SHA256 digest; it does not install the repository’s source archive.
