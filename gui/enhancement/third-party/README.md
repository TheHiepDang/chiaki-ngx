# Third-party source provenance

No NeuralScreen source is included.

- `fsr1/ffx_a.h`, `fsr1/ffx_fsr1.h`: unmodified AMD FidelityFX FSR source,
  [GPUOpen-Effects/FidelityFX-FSR](https://github.com/GPUOpen-Effects/FidelityFX-FSR),
  commit `a21ffb8f6c13233ba336352bdff293894c706575`, `ffx-fsr/`.
  MIT license, Copyright 2021 Advanced Micro Devices, Inc. See `fsr1/LICENSE`.
- `nvofa/nvOpticalFlowCommon.h`, `nvofa/nvOpticalFlowD3D12.h`: unmodified NVIDIA
  Optical Flow SDK 5.0 interface headers from the
  [public SDK mirror](https://github.com/mbucchia/Optical-Flow-SDK), commit
  `54e68293b4898a530bc07e4d7df71efbc5d30f9b`, `NvOFInterface/`.
  Each file carries NVIDIA's MIT license notice.

NGX/DLSS headers and binaries are not vendored. No NVIDIA Optical Flow runtime
is bundled; `nvofapi64.dll` is loaded from Windows System32.
