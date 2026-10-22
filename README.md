![chiaki-ngx logo](gui/res/chiaking-logo.svg)

# chiaki-ngx

A fork of [chiaki-ng](https://github.com/chiaki-ng/chiaki-ng), aiming to deliver next-generation features and more.

For PlayStation setup and general help, see the [original documentation](https://chiaki-ng.github.io/chiaki-ng/).

## Installation

1. Download the latest **Windows release ZIP** from [Releases](https://github.com/TheHiepDang/chiaki-ngx/releases) and extract it.
3. Supply your own **`nvngx_dlssnr.dll`** in **`runtimes/`**.

Check [NVIDIA’s DLSS SDK](https://github.com/NVIDIA/DLSS) or [TechPowerUp](https://www.techpowerup.com/download/nvidia-dlss-dll/) for latest DLSS.

RTX 40/50 use native FG. RTX 20/30 use the bundled unofficial SM86 route.

## Showcase

Videos and screenshots coming soon.

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
