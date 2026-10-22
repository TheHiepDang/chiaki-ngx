// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#pragma once
#include <string>
#include <cwctype>

namespace chiaki_enhancement
{
inline bool isRtxAdapter(unsigned vendor, std::wstring adapter)
{
    if (vendor != 0x10de)
        return false;
    for (auto &c : adapter)
        c = static_cast<wchar_t>(std::towupper(c));
    return adapter.find(L"RTX") != std::wstring::npos;
}

// Keep the GUI's package checks and the worker's loader on the same route.
// Only the consumer RTX 20/30 families select the optional SM75/SM86 backend;
// newer and unrecognized adapters retain NVIDIA's native runtime path.
inline bool usesSm86(std::wstring adapter)
{
    for (auto &c : adapter)
        c = static_cast<wchar_t>(std::towupper(c));
    auto position = adapter.find(L"RTX");
    if (position == std::wstring::npos)
        return false;
    position += 3;
    while (position < adapter.size() && std::iswspace(adapter[position]))
        ++position;
    return position + 4 <= adapter.size() &&
           (adapter.compare(position, 2, L"20") == 0 || adapter.compare(position, 2, L"30") == 0) &&
           std::iswdigit(adapter[position + 2]) && std::iswdigit(adapter[position + 3]) &&
           (position + 4 == adapter.size() || !std::iswdigit(adapter[position + 4]));
}
}
