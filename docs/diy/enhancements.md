# Windows video enhancements

## Controls

Open the live RTX settings panel from the stream menu or Ctrl+Shift+F (rebind under Settings → Keys). RTX settings and Eye Candy are tabs. The RTX panel, its stream-menu buttons and shortcut are available only on Windows with the active Vulkan adapter identified as NVIDIA RTX. Missing NR/FG DLLs do not hide settings on eligible hardware; feature failures still report their own reason. The saved shortcut key remains compatible. General Video renderer options and its Eye Candy tab remain available on other GPUs. Tab/Shift+Tab and header arrows move focus; Ctrl+Tab or controller L1/R1 switches tabs. The dialog follows its content height and scrolls only when the window is too short.

- Neural Rendering (NR) is experimental and off by default. Resolution changes apply once, three seconds after the last slider movement. RTX controls lock with an applying indicator until processed output or a reported failure.
- FSR 1 reconstructs reduced NR output and offers sharpening. It is the current upscaler; DLSS Super Resolution is not integrated.
- Frame generation (FG) is an independent experimental 2× mode, with NR on or off. Generated presentation may be skipped when its cost would reduce real-frame cadence.
- Eye Candy reuses brightness, contrast, saturation and gamma. Each slider has an individual reset. Exposure and Dark Area range from −1 to +1 EV and affect final SDR video, including native resolution and screenshots. HDR bypasses those two adjustments.
- F4 saves a PNG screenshot of the processed SDR video before Chiaki overlays; the shortcut is rebindable.

Controls persist in the current Chiaki settings profile. Submitted real/generated FPS are application submission counts, not console rendering or monitor scanout measurements.

## Runtime architecture

The GUI processes decoded video with Vulkan/libplacebo and exchanges shared textures/fences with the MSVC D3D12 worker. The worker is an executable intentionally named `enhancement/nvngx.dll`; it is started through a no-shell process API. Bundled FG/proxy/backend/kernel DLLs live under `runtimes/` beside `chiaki.exe`. Only the optional NR model DLL is supplied by the user. See [runtime packaging](runtime-packaging.md) for sources, file identity, optional package checks and installation paths.

Processing is native-size SDR prepass → reduced NR → matched residual reconstruction → optional FG → final color adjustment → presentation → application overlays. Optical flow is estimated from video; game-engine motion/depth and separated game HUD buffers are unavailable. Worker/protocol/runtime failures restore ordinary playback and report their reason. FSR/sharpness changes do not recreate NGX resources.

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
