// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "../protocol.h"
#include <cstdio>
#include <cstdlib>
using namespace chiaki_enhancement;
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                              \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
int main()
{
    auto e = nrExtent(2560, 1440, 50);
    CHECK(e.width == 1280 && e.height == 720);
    e = nrExtent(1920, 1080, 65);
    CHECK(e.width == 1248 && e.height == 702);
    e = nrExtent(1920, 1080, 100);
    CHECK(e.width == 1920 && e.height == 1080);
    e = nrExtent(3840, 2160, 100);
    CHECK(e.width == 2560 && e.height == 1440);
    e = nrExtent(1080, 2560, 100);
    CHECK(e.height == 1440 && e.width <= 608);
    CHECK(!canUpscale(e, e));
    CHECK(canUpscale(nrExtent(1920, 1080, 100), nrExtent(1920, 1080, 50)));
    CHECK(nrExtent(0, 1080, 65).width == 0);
    CHECK(std::abs(presentationInterval(1.0 / 60, 1.0 / 120, true, true, false) - 0.0) <
          1e-9);
    CHECK(std::abs(presentationInterval(1.0 / 60, 1.0 / 60, true, true, false) - 0.0) < 1e-9);
    CHECK(std::abs(presentationInterval(1.0 / 30, 1.0 / 120, true, true, false) - 0.0) < 1e-9);
    CHECK(std::abs(presentationInterval(1.0 / 60, 1.0 / 120, true, false, false) - 1.0 / 60) <
          1e-9);
    Command c;
    c.width = 1920;
    c.height = 1080;
    CHECK(valid(c));
    c.percent = 0;
    CHECK(!valid(c));
    c.percent = 65;
    c.operation = Operation::Process;
    c.fenceValue = 1;
    CHECK(valid(c));
    c.slot = SlotCount;
    CHECK(!valid(c));
    c.slot = 0;
    c.fenceValue = 2;
    CHECK(!valid(c));
    c.fenceValue = 1;
    c.flags = 0x8000;
    CHECK(!valid(c));
    c.flags = Neural | Fsr;
    c.nrPassCount = 3;
    c.additionalPasses[0].percent = 75;
    c.additionalPasses[0].fsr = 0;
    c.additionalPasses[1].percent = 100;
    c.additionalPasses[1].sharpness = 85;
    CHECK(valid(c));
    CHECK(passConfig(c, 0).percent == 65 && passConfig(c, 0).fsr == 1);
    CHECK(passConfig(c, 1).percent == 75 && passConfig(c, 1).fsr == 0);
    CHECK(passConfig(c, 2).percent == 100 && passConfig(c, 2).sharpness == 85);
    Command changed = c;
    changed.additionalPasses[1].sharpness = 0;
    CHECK(sameNrResources(c, changed));
    changed.additionalPasses[0].percent = 70;
    CHECK(!sameNrResources(c, changed));
    changed = c;
    changed.nrPassCount = 2;
    CHECK(!sameNrResources(c, changed));
    c.nrPassCount = 0; CHECK(!valid(c));
    c.nrPassCount = 4; CHECK(!valid(c));
    c.nrPassCount = 3;
    c.additionalPasses[0].percent = 49; CHECK(!valid(c));
    c.additionalPasses[0].percent = 65;
    c.additionalPasses[1].fsr = 2; CHECK(!valid(c));
    c.additionalPasses[1].fsr = 1;
    c.additionalPasses[1].sharpness = 101; CHECK(!valid(c));
    c.additionalPasses[1].sharpness = 20;
    c.version = 3; CHECK(!valid(c));
    c.version = Version;
    c.nrTuning.localStructure = 2.0f;
    c.nrTuning.localTone = 0.25f;
    c.nrTuning.skinStructure = 2.5f;
    c.nrTuning.style = 2;
    c.nrTuning.autoMask = 1;
    CHECK(valid(c));
    CHECK(passConfig(c, 0).tuning == c.nrTuning);
    CHECK(passConfig(c, 1).tuning.style == 0);
    changed = c; changed.nrTuning.style = 1;
    changed.additionalPasses[1].tuning.localTone = 0;
    CHECK(sameNrResources(c, changed));
    changed.nrTuning.style = 3; CHECK(!valid(changed));
    changed = c; changed.additionalPasses[0].tuning.autoMask = 2; CHECK(!valid(changed));
    changed = c; changed.nrTuning.localTone = -0.1f; CHECK(!valid(changed));
    changed = c; changed.additionalPasses[1].tuning.skinStructure = 2.6f; CHECK(!valid(changed));
    changed = c; changed.nrTuning.localStructure = std::nanf(""); CHECK(!valid(changed));
    changed = c; changed.additionalPasses[0].tuning.localTone = INFINITY; CHECK(!valid(changed));
    changed = c; changed.nrTuning.intensity = -0.1f; CHECK(!valid(changed));
    changed = c; changed.additionalPasses[0].tuning.intensity = 1.1f; CHECK(!valid(changed));
    changed = c; changed.nrTuning.intensity = std::nanf(""); CHECK(!valid(changed));
    changed = c; changed.additionalPasses[1].tuning.intensity = INFINITY; CHECK(!valid(changed));
    changed = c; changed.nrTuning.intensity = 0; CHECK(valid(changed)); CHECK(sameNrResources(c, changed));
    CHECK(passConfig(changed, 0).tuning.intensity == 0 && passConfig(changed, 1).tuning.intensity == 1);
    changed = c; changed.version = 4; CHECK(!valid(changed));
    c.version = 2; CHECK(!valid(c));
    std::puts("Enhancement geometry, per-pass settings and IPC validation passed");
}
