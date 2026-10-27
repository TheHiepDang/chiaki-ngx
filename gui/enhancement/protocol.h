// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <type_traits>

namespace chiaki_enhancement
{
constexpr uint32_t Magic = 0x58474e43; // CNGX, little endian, Windows x64 only
constexpr uint32_t Version = 5;
constexpr unsigned MaxNrPasses = 3;
constexpr unsigned SlotCount = 3;
enum class Operation : uint32_t
{
    Configure = 1,
    Process,
    Shutdown
};
enum Flag : uint32_t
{
    Neural = 1,
    Fsr = 2,
    FrameGeneration = 4,
    Reset = 8,
    BridgeTest = 16
};
enum class Result : uint32_t
{
    Ready = 1,
    Complete,
    Error
};

struct Extent
{
    uint32_t width = 0, height = 0;
};
inline Extent nrExtent(uint32_t width, uint32_t height, int percent)
{
    if (!width || !height)
        return {};
    const double scale = std::min(std::max(50, std::min(100, percent)) / 100.0,
                                  std::min(2560.0 / width, 1440.0 / height));
    Extent result;
    result.width = std::max(1u, static_cast<uint32_t>(std::floor(width * scale)));
    result.height = std::max(1u, static_cast<uint32_t>(std::floor(height * scale)));
    return result;
}
inline bool canUpscale(Extent native, Extent nr)
{
    return nr.width && nr.height && (nr.width < native.width || nr.height < native.height);
}
inline double presentationInterval(double streamInterval, double refreshInterval, bool vsync,
                                   bool enhancementOwnsPresentation, bool mixerActive)
{
    if (!enhancementOwnsPresentation)
        return mixerActive ? refreshInterval : streamInterval;
    // Worker completions, FG's phase timer and the swapchain pace output. Waiting here again
    // misses the next deadline and can halve the real-frame rate.
    (void)vsync;
    return 0.0;
}

// Fixed-size messages, no pointers/STL/Qt types cross the process boundary.
// Replies contain worker-local handles, kept alive by the worker. Chiaki pulls
// duplicates into itself before importing them, preventing initialization-cancel leaks.
// Model selection and strengths are evaluation parameters, not texture geometry.
struct NrTuning
{
    float intensity = 1.0f, localStructure = 1.0f, localTone = 1.0f, skinStructure = -1.0f;
    uint32_t style = 0, autoMask = 0;
    bool operator==(const NrTuning &other) const
    {
        return intensity == other.intensity && localStructure == other.localStructure && localTone == other.localTone &&
               skinStructure == other.skinStructure && style == other.style && autoMask == other.autoMask;
    }
};
inline bool validTuning(const NrTuning &p)
{
    return std::isfinite(p.intensity) && p.intensity >= 0 && p.intensity <= 1 &&
           std::isfinite(p.localStructure) && p.localStructure >= 0 && p.localStructure <= 2 &&
           std::isfinite(p.localTone) && p.localTone >= 0 && p.localTone <= 2 &&
           std::isfinite(p.skinStructure) && p.skinStructure >= -1 && p.skinStructure <= 2.5f &&
           p.style <= 2 && p.autoMask <= 1;
}
struct NrPassConfig
{
    uint32_t percent = 65, sharpness = 20, fsr = 1;
    NrTuning tuning;
    bool operator==(const NrPassConfig &other) const
    { return percent == other.percent && sharpness == other.sharpness && fsr == other.fsr && tuning == other.tuning; }
};
struct Command
{
    uint32_t magic = Magic, version = Version, size = sizeof(Command);
    Operation operation = Operation::Configure;
    uint32_t parentPid = 0, width = 0, height = 0, flags = 0;
    uint32_t percent = 65, sharpness = 20, slot = 0, reserved = 0;
    uint64_t adapterLuid = 0, frameId = 0, timestampUs = 0, revision = 0, fenceValue = 0;
    uint32_t nrPassCount = 1;
    NrTuning nrTuning;
    NrPassConfig additionalPasses[MaxNrPasses - 1];
};
struct SharedSlot
{
    uint64_t input = 0, real = 0, generated = 0, fence = 0;
};
struct Reply
{
    uint32_t magic = Magic, version = Version, size = sizeof(Reply);
    Result result = Result::Error;
    uint32_t slot = 0, activeFlags = 0, width = 0, height = 0;
    uint64_t frameId = 0, timestampUs = 0, revision = 0, fenceValue = 0;
    uint64_t workerTimeUs = 0, flowTimeUs = 0, nrGpuUs = 0, reconstructionGpuUs = 0, fgGpuUs = 0,
             vramBytes = 0;
    SharedSlot resources[SlotCount];
    char message[472] = {};
};
inline NrPassConfig passConfig(const Command &c, unsigned pass)
{
    if (pass == 0) { NrPassConfig result; result.percent = c.percent; result.sharpness = c.sharpness;
        result.fsr = (c.flags & Fsr) ? 1u : 0u; result.tuning = c.nrTuning; return result; }
    return c.additionalPasses[pass - 1];
}
inline bool validPass(const NrPassConfig &p)
{ return p.percent >= 50 && p.percent <= 100 && p.sharpness <= 100 && p.fsr <= 1 && validTuning(p.tuning); }
inline bool sameNrResources(const Command &a, const Command &b)
{
    if (!(a.flags & Neural) && !(b.flags & Neural)) return true;
    if (a.nrPassCount != b.nrPassCount) return false;
    for (unsigned n = 0; n < a.nrPassCount; ++n)
        if (passConfig(a, n).percent != passConfig(b, n).percent) return false;
    return true;
}
inline bool valid(const Command &c)
{
    return c.magic == Magic && c.version == Version && c.size == sizeof(Command) &&
           c.operation >= Operation::Configure && c.operation <= Operation::Shutdown &&
           c.percent >= 50 && c.percent <= 100 && c.sharpness <= 100 &&
           c.nrPassCount >= 1 && c.nrPassCount <= MaxNrPasses &&
           validTuning(c.nrTuning) && validPass(c.additionalPasses[0]) && validPass(c.additionalPasses[1]) &&
           !(c.flags & ~(Neural | Fsr | FrameGeneration | Reset | BridgeTest)) &&
           (c.operation == Operation::Shutdown ||
            (c.width && c.height && c.width <= 16384 && c.height <= 16384)) &&
           (c.operation != Operation::Process || (c.slot < SlotCount && (c.fenceValue & 1)));
}
static_assert(sizeof(Command) == 192, "IPC command ABI changed");
static_assert(sizeof(Reply) == 680, "IPC reply ABI changed");
static_assert(std::is_trivially_copyable<Command>::value, "IPC must be POD");
} // namespace chiaki_enhancement
