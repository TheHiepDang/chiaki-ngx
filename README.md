![chiaki-ngx logo](gui/res/chiaking-logo.svg)

# chiaki-ngx

A fork of [chiaki-ng](https://github.com/chiaki-ng/chiaki-ng), DLSS5, Upscaler and Framegen for all RTX GPUs.

Tested on RTX 3000s.

For PlayStation setup and general help, see the [original documentation](https://chiaki-ng.github.io/chiaki-ng/).

## Installation

1. Download the latest **chiaki-ngx.zipP** from [Releases](https://github.com/TheHiepDang/chiaki-ngx/releases) and extract it.
3. Supply your own **`nvngx_dlssnr.dll`** in **`runtimes/`**.

Check [NVIDIA’s DLSS SDK](https://github.com/NVIDIA/DLSS) or [TechPowerUp](https://www.techpowerup.com/download/nvidia-dlss-dll/) for latest DLSS.

RTX 40/50 use native FG. RTX 20/30 use the bundled unofficial SM86 route.

## NR passes

During streaming, open **RTX settings** with **Ctrl+Shift+F**. Enable Neural Rendering and choose **1, 2 or 3 passes**. Extra passes follow pass 1 by default. Check **Customize each pass**, then **Configure pass**, to edit them independently; saved custom values are kept when you switch back.

Each pass offers **Default / Natural / Cinematic** NR presets, **Intensity**, **Local Structure**, **Local Tone**, **Skin Structure**, **Automatic Mask**, resolution, FSR and sharpness. Presets select the NR model without moving your strength sliders. Skin Structure requires Automatic Mask; **−1 / Off** disables skin adjustments. Use **↺** beside a slider to restore its default for the selected pass. Resolution, including its reset, applies three seconds after the last adjustment. Frame generation runs after the final pass. More passes use more GPU time and memory; one pass remains the default.

## Showcase

Video show case DLSS5, Upscaler and Framegen with Dragon's Dogma 2:

[![Watch the video](https://img.youtube.com/vi/tHVEoDi1oKc/hqdefault.jpg)](https://www.youtube.com/watch?v=tHVEoDi1oKc)

Native NR (100% resolution) vs. Upscaled NR (75% resolution, 30-40% performance gain) comparison:

![Native NR](<assets/github demo images/native nr.png>)

![Upscaled NR](<assets/github demo images/upscaled nr.png>)

## Bug reports

Open **Settings → General → Log Directory → Open**. Include the matching **`chiaki_ngx_*.log`** and **`.previous.log`** if present, plus **`chiaki_session_*.log`** for streaming issues. Include crash text if created; share crash dumps privately. NR pass diagnostics use **`[RTX][NR][Pass1]`**, **`[RTX][NR][Pass2]`** and **`[RTX][NR][Pass3]`**. See [reporting details](docs/diy/enhancements.md#bug-reports) for SM86 logs and other information to attach.

## Credits

| Project | Contribution |
|---|---|
| [Chiaki](https://github.com/thestr4ng3r/chiaki) and [chiaki-ng](https://github.com/chiaki-ng/chiaki-ng) | Original Remote Play client, upstream foundation and contributors. |
| [DLSSG for SM86 — sdli1995](https://github.com/sdli1995/dlssg_for_sm86/) | Older-RTX FG: `version.dll` proxy, `sm86_backend.dll` backend and `fg_kernels.dll` kernels. |
| [NVIDIA DLSS SDK](https://github.com/NVIDIA/DLSS) | NGX/DLSS interfaces and the bundled NVIDIA FG runtime. |
| [AMD FidelityFX FSR 1](https://github.com/GPUOpen-Effects/FidelityFX-FSR) | EASU upscaling and RCAS sharpening. |
| [Qt](https://www.qt.io/), [FFmpeg](https://ffmpeg.org/), [libplacebo](https://github.com/haasn/libplacebo) and [SDL](https://www.libsdl.org/) | Interface, media processing, video rendering and input. |

---

This project is not endorsed or certified by Sony Interactive Entertainment LLC or NVIDIA Corporation.
