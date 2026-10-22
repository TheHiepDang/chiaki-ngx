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
    std::puts("Enhancement geometry and IPC validation passed");
}
