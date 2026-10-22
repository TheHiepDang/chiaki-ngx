// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// Standalone, independently implemented NGX host. No desktop capture or injection.
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_defs_dlssg.h>
#include "third-party/nvofa/nvOpticalFlowD3D12.h"
#include "protocol.h"
#include "runtime_route.h"
#include "crash_dump_win.h"
#include "worker_shaders.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <variant>
#include <io.h>
#include <fcntl.h>

using Microsoft::WRL::ComPtr;
using namespace chiaki_enhancement;
namespace fs = std::filesystem;
static void check(HRESULT hr, const char *what)
{
    if (FAILED(hr))
    {
        char b[256];
        std::snprintf(b, sizeof b, "%s: HRESULT 0x%08lx", what, static_cast<unsigned long>(hr));
        throw std::runtime_error(b);
    }
}
static void ngxCheck(NVSDK_NGX_Result r, const char *what)
{
    if (NVSDK_NGX_FAILED(r))
    {
        char b[256];
        std::snprintf(b, sizeof b, "%s: NGX 0x%08x", what, static_cast<unsigned>(r));
        throw std::runtime_error(b);
    }
}
struct OwnedHandle
{
    HANDLE h = nullptr;
    ~OwnedHandle()
    {
        if (h && h != INVALID_HANDLE_VALUE)
            CloseHandle(h);
    }
    OwnedHandle() = default;
    OwnedHandle(const OwnedHandle &) = delete;
    OwnedHandle &operator=(const OwnedHandle &) = delete;
};

// The public NGX parameter ABI. All numeric conversions follow ordinary C++
// scalar conversions. Missing parameters fail instead of inventing capabilities.
class Parameters final : public NVSDK_NGX_Parameter
{
    using Value = std::variant<uint64_t, int64_t, double, void *>;
    std::map<std::string, Value> values;
    mutable std::map<std::string, bool> missing;
    template <class T> void set(const char *n, T value)
    {
        if constexpr (std::is_floating_point<T>::value)
            values[n] = static_cast<double>(value);
        else if constexpr (std::is_signed<T>::value)
            values[n] = static_cast<int64_t>(value);
        else
            values[n] = static_cast<uint64_t>(value);
    }
    void ptr(const char *n, void *value)
    {
        values[n] = value;
    }
    NVSDK_NGX_Result absent(const char *n) const
    {
        if (GetEnvironmentVariableW(L"CHIAKI_ENHANCEMENT_TRACE", nullptr, 0) && !missing[n])
        {
            missing[n] = true;
            std::fprintf(stderr, "[RTX][NGX] missing parameter %s\n", n);
        }
        return NVSDK_NGX_Result_FAIL_InvalidParameter;
    }
    template <class T> NVSDK_NGX_Result get(const char *n, T *out) const
    {
        if (!out)
            return NVSDK_NGX_Result_FAIL_InvalidParameter;
        *out = T{};
        auto i = values.find(n);
        if (i == values.end())
            return absent(n);
        return std::visit(
            [&](auto value) -> NVSDK_NGX_Result
            {
                if constexpr (std::is_pointer<decltype(value)>::value)
                    return absent(n);
                else
                {
                    *out = static_cast<T>(value);
                    return NVSDK_NGX_Result_Success;
                }
            },
            i->second);
    }
    template <class T> NVSDK_NGX_Result ptrGet(const char *n, T **out) const
    {
        if (!out)
            return NVSDK_NGX_Result_FAIL_InvalidParameter;
        *out = nullptr;
        auto i = values.find(n);
        if (i == values.end())
            return absent(n);
        auto pointer = std::get_if<void *>(&i->second);
        if (!pointer)
            return absent(n);
        *out = static_cast<T *>(*pointer);
        return NVSDK_NGX_Result_Success;
    }

  public:
    void Set(const char *n, unsigned long long v) override
    {
        set(n, v);
    }
    void Set(const char *n, float v) override
    {
        set(n, v);
    }
    void Set(const char *n, double v) override
    {
        set(n, v);
    }
    void Set(const char *n, unsigned int v) override
    {
        set(n, v);
    }
    void Set(const char *n, int v) override
    {
        set(n, v);
    }
    void Set(const char *n, ID3D11Resource *v) override
    {
        ptr(n, v);
    }
    void Set(const char *n, ID3D12Resource *v) override
    {
        ptr(n, v);
    }
    void Set(const char *n, void *v) override
    {
        ptr(n, v);
    }
    NVSDK_NGX_Result Get(const char *n, unsigned long long *v) const override
    {
        return get(n, v);
    }
    NVSDK_NGX_Result Get(const char *n, float *v) const override
    {
        return get(n, v);
    }
    NVSDK_NGX_Result Get(const char *n, double *v) const override
    {
        return get(n, v);
    }
    NVSDK_NGX_Result Get(const char *n, unsigned int *v) const override
    {
        return get(n, v);
    }
    NVSDK_NGX_Result Get(const char *n, int *v) const override
    {
        return get(n, v);
    }
    NVSDK_NGX_Result Get(const char *n, ID3D11Resource **v) const override
    {
        return ptrGet(n, v);
    }
    NVSDK_NGX_Result Get(const char *n, ID3D12Resource **v) const override
    {
        return ptrGet(n, v);
    }
    NVSDK_NGX_Result Get(const char *n, void **v) const override
    {
        return ptrGet(n, v);
    }
    void Reset() override
    {
        values.clear();
    }
};

struct Texture
{
    ComPtr<ID3D12Resource> resource;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    unsigned width = 0, height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
};
struct Gpu
{
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12QueryHeap> timestamps;
    ComPtr<ID3D12Resource> timestampReadback;
    ComPtr<IDXGIAdapter3> memoryAdapter;
    UINT64 timestampFrequency = 1;
    bool sm86 = false;
    OwnedHandle event;
    uint64_t tick = 0;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12DescriptorHeap> descriptors;
    unsigned descriptorSize = 0, descriptorCursor = 0;
    std::map<std::string, ComPtr<ID3D12PipelineState>> shaders;

    explicit Gpu(uint64_t luid)
    {
        ComPtr<IDXGIFactory4> factory;
        check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory");
        ComPtr<IDXGIAdapter1> adapter;
        if (luid)
        {
            LUID l;
            std::memcpy(&l, &luid, 8);
            check(factory->EnumAdapterByLuid(l, IID_PPV_ARGS(&adapter)),
                  "Match Vulkan adapter LUID");
        }
        else
            for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
            {
                DXGI_ADAPTER_DESC1 d{};
                adapter->GetDesc1(&d);
                if (!(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && d.VendorId == 0x10de)
                    break;
                adapter.Reset();
            }
        if (!adapter)
            throw std::runtime_error("No NVIDIA D3D12 adapter");
        DXGI_ADAPTER_DESC1 description{};
        adapter->GetDesc1(&description);
        sm86 = usesSm86(description.Description);
        adapter.As(&memoryAdapter);
        std::fprintf(stderr, "[RTX][Worker] adapter=%ls VRAM=%llu MiB\n", description.Description,
                     description.DedicatedVideoMemory / (1024ull * 1024));
        check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)),
              "D3D12CreateDevice");
        D3D12_COMMAND_QUEUE_DESC q{};
        q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        check(device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue)), "CreateCommandQueue");
        check(device->CreateCommandAllocator(q.Type, IID_PPV_ARGS(&allocator)),
              "CreateCommandAllocator");
        check(device->CreateCommandList(0, q.Type, allocator.Get(), nullptr, IID_PPV_ARGS(&list)),
              "CreateCommandList");
        check(list->Close(), "Close initial list");
        check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "CreateFence");
        check(queue->GetTimestampFrequency(&timestampFrequency), "Timestamp frequency");
        D3D12_QUERY_HEAP_DESC qh{};
        qh.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        qh.Count = 6;
        check(device->CreateQueryHeap(&qh, IID_PPV_ARGS(&timestamps)), "Timestamp query heap");
        D3D12_HEAP_PROPERTIES rh{};
        rh.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rb{};
        rb.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rb.Width = 48;
        rb.Height = 1;
        rb.DepthOrArraySize = 1;
        rb.MipLevels = 1;
        rb.SampleDesc.Count = 1;
        rb.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        check(device->CreateCommittedResource(&rh, D3D12_HEAP_FLAG_NONE, &rb,
                                              D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                              IID_PPV_ARGS(&timestampReadback)),
              "Timestamp readback");
        event.h = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event.h)
            throw std::runtime_error("CreateEvent failed");
        D3D12_DESCRIPTOR_RANGE ranges[2]{};
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[0].NumDescriptors = 5;
        ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        ranges[1].NumDescriptors = 1;
        ranges[1].OffsetInDescriptorsFromTableStart = 5;
        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[0].DescriptorTable.NumDescriptorRanges = 2;
        params[0].DescriptorTable.pDescriptorRanges = ranges;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[1].Constants.Num32BitValues = 8;
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_SIGNATURE_DESC rd{};
        rd.NumParameters = 2;
        rd.pParameters = params;
        rd.NumStaticSamplers = 1;
        rd.pStaticSamplers = &sampler;
        ComPtr<ID3DBlob> blob, error;
        check(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error),
              "Serialize root signature");
        check(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                          IID_PPV_ARGS(&root)),
              "CreateRootSignature");
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.NumDescriptors = 256;
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        check(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&descriptors)),
              "CreateDescriptorHeap");
        descriptorSize = device->GetDescriptorHandleIncrementSize(hd.Type);
        for (const char *entry : {"scaleMain", "grayMain", "zeroMain", "motionMain", "residualMain",
                                  "easuMain", "rcasMain"})
        {
            const char *source =
                (std::strcmp(entry, "easuMain") == 0 || std::strcmp(entry, "rcasMain") == 0)
                    ? fsrShader
                    : commonShader;
            HRESULT hr = D3DCompile(source, std::strlen(source), entry, nullptr, nullptr, entry,
                                    "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &error);
            if (FAILED(hr) && error)
                std::fprintf(stderr, "[RTX][Shader] %s\n", static_cast<char *>(error->GetBufferPointer()));
            check(hr, entry);
            D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
            pd.pRootSignature = root.Get();
            pd.CS = {blob->GetBufferPointer(), blob->GetBufferSize()};
            check(device->CreateComputePipelineState(&pd, IID_PPV_ARGS(&shaders[entry])),
                  "Create compute pipeline");
        }
    }
    void wait()
    {
        check(queue->Signal(fence.Get(), ++tick), "Signal GPU fence");
        check(fence->SetEventOnCompletion(tick, event.h), "SetEventOnCompletion");
        if (WaitForSingleObject(event.h, 10000) != WAIT_OBJECT_0)
            throw std::runtime_error("GPU did not complete within 10 seconds");
    }
    void mark(unsigned index)
    {
        list->EndQuery(timestamps.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index);
    }
    void resolveTimers()
    {
        list->ResolveQueryData(timestamps.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 6,
                               timestampReadback.Get(), 0);
    }
    std::array<uint64_t, 3> timings()
    {
        UINT64 *data = nullptr;
        D3D12_RANGE range{0, 48};
        check(timestampReadback->Map(0, &range, reinterpret_cast<void **>(&data)),
              "Map timestamps");
        std::array<uint64_t, 3> times{};
        for (unsigned n = 0; n < 3; ++n)
            times[n] = (data[n * 2 + 1] - data[n * 2]) * 1000000 / timestampFrequency;
        D3D12_RANGE empty{0, 0};
        timestampReadback->Unmap(0, &empty);
        return times;
    }
    uint64_t vram()
    {
        DXGI_QUERY_VIDEO_MEMORY_INFO info{};
        if (memoryAdapter && SUCCEEDED(memoryAdapter->QueryVideoMemoryInfo(
                                 0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info)))
            return info.CurrentUsage;
        return 0;
    }
    void begin()
    {
        check(allocator->Reset(), "Reset allocator");
        check(list->Reset(allocator.Get(), nullptr), "Reset list");
        descriptorCursor = 0;
    }
    void submit()
    {
        check(list->Close(), "Close list");
        ID3D12CommandList *l = list.Get();
        queue->ExecuteCommandLists(1, &l);
        wait();
    }
    Texture texture(unsigned w, unsigned h, DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM,
                    bool shared = false)
    {
        Texture t;
        t.width = w;
        t.height = h;
        t.format = format;
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC d{};
        d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.Width = w;
        d.Height = h;
        d.DepthOrArraySize = 1;
        d.MipLevels = 1;
        d.Format = format;
        d.SampleDesc.Count = 1;
        d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        if (shared)
            d.Flags |= D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
        check(device->CreateCommittedResource(
                  &hp, shared ? D3D12_HEAP_FLAG_SHARED : D3D12_HEAP_FLAG_NONE, &d,
                  D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&t.resource)),
              "Create texture");
        return t;
    }
    void transition(Texture &t, D3D12_RESOURCE_STATES state)
    {
        if (t.state == state)
        {
            if (state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
            {
                D3D12_RESOURCE_BARRIER b{};
                b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                b.UAV.pResource = t.resource.Get();
                list->ResourceBarrier(1, &b);
            }
            return;
        }
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = t.resource.Get();
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = t.state;
        b.Transition.StateAfter = state;
        list->ResourceBarrier(1, &b);
        t.state = state;
    }
    void copy(Texture &src, Texture &dst)
    {
        transition(src, D3D12_RESOURCE_STATE_COPY_SOURCE);
        transition(dst, D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyResource(dst.resource.Get(), src.resource.Get());
    }
    void run(const char *entry, Texture *input, Texture &out, Texture *second = nullptr,
             Texture *third = nullptr, Texture *flow = nullptr, float sharpness = 0, float sx = 1,
             float sy = 1)
    {
        if (descriptorCursor + 6 > 256)
            throw std::runtime_error("Descriptor capacity exceeded");
        auto cpu = descriptors->GetCPUDescriptorHandleForHeapStart();
        cpu.ptr += descriptorCursor * descriptorSize;
        auto gpu = descriptors->GetGPUDescriptorHandleForHeapStart();
        gpu.ptr += descriptorCursor * descriptorSize;
        descriptorCursor += 6;
        Texture *inputs[] = {input, second, third, nullptr, flow};
        for (unsigned i = 0; i < 5; ++i)
        {
            auto h = cpu;
            h.ptr += i * descriptorSize;
            D3D12_SHADER_RESOURCE_VIEW_DESC d{};
            d.Format = inputs[i] ? inputs[i]->format : DXGI_FORMAT_R8G8B8A8_UNORM;
            if (i == 4 && !inputs[i])
                d.Format = DXGI_FORMAT_R16G16_SINT;
            d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            d.Texture2D.MipLevels = 1;
            if (inputs[i])
                transition(*inputs[i], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            device->CreateShaderResourceView(inputs[i] ? inputs[i]->resource.Get() : nullptr, &d,
                                             h);
        }
        transition(out, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        auto h = cpu;
        h.ptr += 5 * descriptorSize;
        D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
        u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        u.Format = out.format;
        device->CreateUnorderedAccessView(out.resource.Get(), nullptr, &u, h);
        struct Constants
        {
            uint32_t sw, sh, ow, oh;
            float sx, sy, sharp;
            uint32_t reset;
        } k{input ? input->width : (flow ? flow->width : out.width),
            input ? input->height : (flow ? flow->height : out.height),
            out.width,
            out.height,
            sx,
            sy,
            sharpness,
            0};
        ID3D12DescriptorHeap *heap = descriptors.Get();
        list->SetDescriptorHeaps(1, &heap);
        list->SetComputeRootSignature(root.Get());
        list->SetPipelineState(shaders.at(entry).Get());
        list->SetComputeRootDescriptorTable(0, gpu);
        list->SetComputeRoot32BitConstants(1, 8, &k, 0);
        list->Dispatch((out.width + 7) / 8, (out.height + 7) / 8, 1);
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        b.UAV.pResource = out.resource.Get();
        list->ResourceBarrier(1, &b);
    }
    std::vector<uint8_t> pixels(Texture &t)
    {
        auto desc = t.resource->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
        UINT rows = 0;
        UINT64 rowBytes = 0, total = 0;
        device->GetCopyableFootprints(&desc, 0, 1, 0, &layout, &rows, &rowBytes, &total);
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC bd{};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bd.Width = total;
        bd.Height = 1;
        bd.DepthOrArraySize = 1;
        bd.MipLevels = 1;
        bd.SampleDesc.Count = 1;
        bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> read;
        check(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd,
                                              D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                              IID_PPV_ARGS(&read)),
              "Readback buffer");
        begin();
        transition(t, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
        src.pResource = t.resource.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.pResource = read.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = layout;
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        submit();
        uint8_t *data = nullptr;
        D3D12_RANGE range{0, static_cast<SIZE_T>(total)};
        check(read->Map(0, &range, reinterpret_cast<void **>(&data)), "Map readback");
        std::vector<uint8_t> result(static_cast<size_t>(t.width) * t.height * 4);
        for (unsigned y = 0; y < t.height; ++y)
            std::memcpy(result.data() + static_cast<size_t>(y) * t.width * 4,
                        data + layout.Offset + static_cast<size_t>(y) * layout.Footprint.RowPitch,
                        t.width * 4);
        D3D12_RANGE empty{0, 0};
        read->Unmap(0, &empty);
        return result;
    }
};

struct NgxFeature
{
    HMODULE module = nullptr;
    NVSDK_NGX_Handle *handle = nullptr;
    Parameters parameters;
    ID3D12Device *device = nullptr;
    using Init = NVSDK_NGX_Result(__cdecl *)(unsigned long long, const wchar_t *, ID3D12Device *,
                                             NVSDK_NGX_Version, const NVSDK_NGX_Parameter *);
    using Create = NVSDK_NGX_Result(__cdecl *)(ID3D12GraphicsCommandList *, NVSDK_NGX_Feature,
                                               NVSDK_NGX_Parameter *, NVSDK_NGX_Handle **);
    using Evaluate = NVSDK_NGX_Result(__cdecl *)(ID3D12GraphicsCommandList *,
                                                 const NVSDK_NGX_Handle *,
                                                 const NVSDK_NGX_Parameter *, void *);
    using Release = NVSDK_NGX_Result(__cdecl *)(NVSDK_NGX_Handle *);
    using Shutdown = NVSDK_NGX_Result(__cdecl *)(ID3D12Device *);
    Create create = nullptr;
    Evaluate evaluate = nullptr;
    Release release = nullptr;
    Shutdown shutdown = nullptr;
    bool initialized = false;
    ~NgxFeature()
    {
        if (handle && release)
            release(handle);
        if (initialized && shutdown)
            shutdown(device);
        if (module)
            FreeLibrary(module);
    }
    void load(Gpu &gpu, const fs::path &path)
    {
        module =
            LoadLibraryExW(path.c_str(), nullptr,
                           LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!module)
            throw std::runtime_error("Cannot load optional runtime: " + path.filename().string() +
                                     " (Win32 " + std::to_string(GetLastError()) + ")");
        auto init = reinterpret_cast<Init>(GetProcAddress(module, "NVSDK_NGX_D3D12_Init_Ext"));
        create = reinterpret_cast<Create>(GetProcAddress(module, "NVSDK_NGX_D3D12_CreateFeature"));
        evaluate =
            reinterpret_cast<Evaluate>(GetProcAddress(module, "NVSDK_NGX_D3D12_EvaluateFeature"));
        release =
            reinterpret_cast<Release>(GetProcAddress(module, "NVSDK_NGX_D3D12_ReleaseFeature"));
        shutdown = reinterpret_cast<Shutdown>(GetProcAddress(module, "NVSDK_NGX_D3D12_Shutdown1"));
        if (!init || !create || !evaluate || !release || !shutdown)
            throw std::runtime_error("Optional runtime has an incompatible NGX ABI");
        device = gpu.device.Get();
        ngxCheck(
            init(0x0876232cULL, path.parent_path().c_str(), device, NVSDK_NGX_Version_API, nullptr),
            "NGX Init_Ext");
        initialized = true;
    }
    void make(Gpu &gpu, unsigned feature, unsigned w, unsigned h, bool fg)
    {
        parameters.Set("Width", w);
        parameters.Set("Height", h);
        parameters.Set("OutWidth", w);
        parameters.Set("OutHeight", h);
        parameters.Set("CreationNodeMask", 1u);
        parameters.Set("VisibilityNodeMask", 1u);
        parameters.Set("PerfQualityValue", 1);
        const std::string prefix = fg ? "DLSSG." : "DLSSNR.";
        for (auto key : {"Width", "InputWidth", "OutputWidth", "Output.Width"})
            parameters.Set((prefix + key).c_str(), w);
        for (auto key : {"Height", "InputHeight", "OutputHeight", "Output.Height"})
            parameters.Set((prefix + key).c_str(), h);
        parameters.Set("DLSSNR.Upscaling", 0u);
        parameters.Set("DLSSNR.ScalingRatio", 1.0f);
        parameters.Set("DLSSNR.Scale", 1.0f);
        parameters.Set("DLSSG.ResourceAlwaysProvidedFlags",
                       unsigned(NVSDK_NGX_DLSSG_ResourceFlags_Backbuffer |
                                NVSDK_NGX_DLSSG_ResourceFlags_MVecs |
                                NVSDK_NGX_DLSSG_ResourceFlags_Depth |
                                NVSDK_NGX_DLSSG_ResourceFlags_HUDLess |
                                NVSDK_NGX_DLSSG_ResourceFlags_OutputInterpolated |
                                NVSDK_NGX_DLSSG_ResourceFlags_OutputReal));
        parameters.Set("DLSSG.ResourceNeverProvidedFlags",
                       unsigned(NVSDK_NGX_DLSSG_ResourceFlags_UI |
                                NVSDK_NGX_DLSSG_ResourceFlags_UIAlpha |
                                NVSDK_NGX_DLSSG_ResourceFlags_BidirectionalDistortionField |
                                NVSDK_NGX_DLSSG_ResourceFlags_OutputDisableInterpolation));
        parameters.Set("DLSSG.UseReflexMatrices", 0u);
        parameters.Set("DLSSG.UserInterfaceRecompositionEnabled", 0u);
        parameters.Set("DLSSG.EnableInterp", 1u);
        parameters.Set("Enable.OFA", 1u);
        parameters.Set("DLSSG.DynamicResolution", 0u);
        parameters.Set("DLSSG.InternalWidth", w);
        parameters.Set("DLSSG.InternalHeight", h);
        parameters.Set("DLSSG.BackbufferFormat", static_cast<unsigned>(DXGI_FORMAT_R8G8B8A8_UNORM));
        parameters.Set("DLSSG.ColorBuffersHDR", 0u);
        parameters.Set("DLSSG.MultiFrameCount", 1u);
        gpu.begin();
        ngxCheck(
            create(gpu.list.Get(), static_cast<NVSDK_NGX_Feature>(feature), &parameters, &handle),
            fg ? "Create FG" : "Create NR");
        gpu.submit();
    }
    void resource(const char *key, Texture &t)
    {
        parameters.Set(key, t.resource.Get());
        std::string k(key);
        parameters.Set((k + "SubrectBaseX").c_str(), 0u);
        parameters.Set((k + "SubrectBaseY").c_str(), 0u);
        parameters.Set((k + "SubrectWidth").c_str(), t.width);
        parameters.Set((k + "SubrectHeight").c_str(), t.height);
    }
    void eval(Gpu &gpu)
    {
        ngxCheck(evaluate(gpu.list.Get(), handle, &parameters, nullptr), "NGX EvaluateFeature");
    }
};

struct OpticalFlow
{
    HMODULE module = nullptr;
    NvOFHandle handle = nullptr;
    NV_OF_D3D12_API_FUNCTION_LIST api{};
    Texture current, previous, vectors;
    NvOFGPUBufferHandle cur = nullptr, prev = nullptr, flow = nullptr;
    bool history = false;
    ~OpticalFlow()
    {
        if (handle)
        {
            for (auto x : {cur, prev, flow})
                if (x)
                {
                    NV_OF_UNREGISTER_RESOURCE_PARAMS_D3D12 p{};
                    p.hOFGpuBuffer = x;
                    api.nvOFUnregisterResourceD3D12(&p);
                }
            api.nvOFDestroy(handle);
        }
        if (module)
            FreeLibrary(module);
    }
    static void ofcheck(NV_OF_STATUS s, const char *w)
    {
        if (s != NV_OF_SUCCESS)
            throw std::runtime_error(std::string(w) + ": NVOFA " + std::to_string(s));
    }
    void init(Gpu &gpu, unsigned w, unsigned h)
    {
        module = LoadLibraryExW(L"nvofapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module)
            throw std::runtime_error("NVIDIA Optical Flow driver runtime unavailable");
        using Create = NV_OF_STATUS(NVOFAPI *)(uint32_t, NV_OF_D3D12_API_FUNCTION_LIST *);
        auto create =
            reinterpret_cast<Create>(GetProcAddress(module, "NvOFAPICreateInstanceD3D12"));
        if (!create)
            throw std::runtime_error("NVOFA D3D12 entry point unavailable");
        ofcheck(create(NV_OF_API_VERSION, &api), "NVOFA API");
        ofcheck(api.nvCreateOpticalFlowD3D12(gpu.device.Get(), &handle), "NVOFA device");
        NV_OF_INIT_PARAMS p{};
        p.width = w;
        p.height = h;
        p.outGridSize = NV_OF_OUTPUT_VECTOR_GRID_SIZE_4;
        p.mode = NV_OF_MODE_OPTICALFLOW;
        p.perfLevel = NV_OF_PERF_LEVEL_FAST;
        p.inputBufferFormat = NV_OF_BUFFER_FORMAT_GRAYSCALE8;
        ofcheck(api.nvOFInit(handle, &p), "NVOFA init");
        current = gpu.texture(w, h, DXGI_FORMAT_R8_UNORM);
        previous = gpu.texture(w, h, DXGI_FORMAT_R8_UNORM);
        vectors = gpu.texture((w + 3) / 4, (h + 3) / 4, DXGI_FORMAT_R16G16_SINT);
        for (auto item : {std::make_pair(&current, &cur), std::make_pair(&previous, &prev),
                          std::make_pair(&vectors, &flow)})
        {
            NV_OF_REGISTER_RESOURCE_PARAMS_D3D12 r{};
            r.resource = item.first->resource.Get();
            r.hOFGpuBuffer = item.second;
            ofcheck(api.nvOFRegisterResourceD3D12(handle, &r), "NVOFA register");
        }
    }
    void execute(Gpu &gpu, Texture &native, bool reset)
    {
        gpu.begin();
        gpu.run("grayMain", &native, current);
        gpu.transition(current, D3D12_RESOURCE_STATE_COMMON);
        if (!history || reset)
            gpu.copy(current, previous);
        gpu.transition(previous, D3D12_RESOURCE_STATE_COMMON);
        gpu.transition(vectors, D3D12_RESOURCE_STATE_COMMON);
        gpu.submit();
        NV_OF_EXECUTE_INPUT_PARAMS_D3D12 in{};
        in.inputFrame = cur;
        in.referenceFrame = prev;
        in.disableTemporalHints = reset ? NV_OF_TRUE : NV_OF_FALSE;
        NV_OF_FENCE_POINT available{gpu.fence.Get(), gpu.tick};
        in.numFencePoints = 1;
        in.fencePoint = &available;
        NV_OF_FENCE_POINT done{gpu.fence.Get(), ++gpu.tick};
        NV_OF_EXECUTE_OUTPUT_PARAMS_D3D12 out{};
        out.outputBuffer = flow;
        out.fencePoint = &done;
        auto result = api.nvOFExecuteD3D12(handle, &in, &out);
        if (result != NV_OF_SUCCESS)
        {
            char error[1024] = {};
            uint32_t size = sizeof error;
            api.nvOFGetLastError(handle, error, &size);
            throw std::runtime_error(std::string("NVOFA execute: ") + error + " (" +
                                     std::to_string(result) + ")");
        }
        check(gpu.queue->Wait(gpu.fence.Get(), gpu.tick), "Wait optical flow");
        gpu.wait();
        gpu.begin();
        gpu.copy(current, previous);
        gpu.transition(previous, D3D12_RESOURCE_STATE_COMMON);
        gpu.submit();
        history = true;
    }
};

struct Slot
{
    Texture input, real, generated;
    ComPtr<ID3D12Fence> fence;
    uint64_t lastValue = 0;
};
struct Worker
{
    std::unique_ptr<Gpu> gpu;
    std::array<Slot, SlotCount> resources;
    std::unique_ptr<NgxFeature> nr, fg;
    std::unique_ptr<OpticalFlow> optical;
    HMODULE proxy = nullptr;
    Texture reduced, edited, motion, depth, fullMotion, fullDepth, upInput, upOutput, composite,
        fgReal, thumbnail;
    std::vector<uint8_t> previousThumbnail, previousRealPixels;
    Command config{};
    fs::path runtimeDir;
    std::string nrError, fgError;
    bool fgVerified = false, history = false;
    uint64_t lastFrame = 0;
    ~Worker()
    {
        for (HANDLE handle : exportedHandles)
            CloseHandle(handle);
        if (gpu)
        {
            try
            {
                gpu->wait();
            }
            catch (...)
            {
            }
            fg.reset();
            nr.reset();
            optical.reset();
        }
        if (proxy)
            FreeLibrary(proxy);
    }
    std::vector<HANDLE> exportedHandles;
    uint64_t exportHandle(ID3D12DeviceChild *resource)
    {
        HANDLE handle = nullptr;
        check(gpu->device->CreateSharedHandle(resource, nullptr, GENERIC_ALL, nullptr, &handle),
              "Export D3D12 handle");
        exportedHandles.push_back(handle);
        return reinterpret_cast<uint64_t>(handle);
    }
    Reply configure(const Command &c, const fs::path &dir)
    {
        config = c;
        runtimeDir = dir;
        gpu.reset(new Gpu(c.adapterLuid));
        Reply reply;
        reply.result = Result::Ready;
        reply.revision = c.revision;
        for (unsigned i = 0; i < SlotCount; ++i)
        {
            auto &s = resources[i];
            s.input = gpu->texture(c.width, c.height, DXGI_FORMAT_R8G8B8A8_UNORM, true);
            s.real = gpu->texture(c.width, c.height, DXGI_FORMAT_R8G8B8A8_UNORM, true);
            s.generated = gpu->texture(c.width, c.height, DXGI_FORMAT_R8G8B8A8_UNORM, true);
            check(gpu->device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&s.fence)),
                  "Create shared fence");
        }
        thumbnail = gpu->texture(64, 36);
        auto extent = nrExtent(c.width, c.height, c.percent);
        reply.width = extent.width;
        reply.height = extent.height;
        if (c.flags & Neural)
        {
            try
            {
                nr.reset(new NgxFeature);
                nr->load(*gpu, runtimeDir / L"nvngx_dlssnr.dll");
                nr->make(*gpu, 18, extent.width, extent.height, false);
            }
            catch (const std::exception &e)
            {
                nrError = e.what();
                nr.reset();
                std::fprintf(stderr, "[RTX][NR] %s\n", e.what());
            }
        }
        if (c.flags & FrameGeneration)
        {
            try
            {
                fs::path proxyPath = runtimeDir / L"sm86-proxy" / L"version.dll";
                std::fprintf(stderr, "[RTX][RuntimeRoute] %s\n", gpu->sm86 ? "SM86 proxy" : "native NVIDIA");
                if (gpu->sm86)
                {
                    if (!fs::exists(proxyPath))
                        throw std::runtime_error("SM86 proxy is missing; restore the bundled runtime files");
                    if (!fs::exists(proxyPath.parent_path() / L"dlssg_sm86.ini") ||
                        !fs::exists(proxyPath.parent_path() / L"dlssg_sm86" / L"backend" /
                                    L"sm86_backend.dll"))
                        throw std::runtime_error("Incomplete SM86 proxy package");
                    proxy = LoadLibraryExW(proxyPath.c_str(), nullptr,
                                           LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                               LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
                    if (!proxy)
                        throw std::runtime_error("SM86 proxy failed to load");
                }
                fg.reset(new NgxFeature);
                fg->load(*gpu, runtimeDir / L"nvngx_dlssg.dll");
                fg->make(*gpu, 11, c.width, c.height, true);
            }
            catch (const std::exception &e)
            {
                fgError = e.what();
                fg.reset();
                std::fprintf(stderr, "[RTX][FG] %s\n", e.what());
            }
        }
        if (nr || fg)
        {
            try
            {
                optical.reset(new OpticalFlow);
                optical->init(*gpu, c.width, c.height);
            }
            catch (const std::exception &e)
            {
                nrError = fgError = e.what();
                nr.reset();
                fg.reset();
                optical.reset();
            }
        }
        if (nr)
        {
            reduced = gpu->texture(extent.width, extent.height);
            edited = gpu->texture(extent.width, extent.height);
            motion = gpu->texture(extent.width, extent.height, DXGI_FORMAT_R16G16_FLOAT);
            depth = gpu->texture(extent.width, extent.height, DXGI_FORMAT_R32_FLOAT);
            upInput = gpu->texture(c.width, c.height);
            upOutput = gpu->texture(c.width, c.height);
            composite = gpu->texture(c.width, c.height);
        }
        if (fg)
        {
            fullMotion = gpu->texture(c.width, c.height, DXGI_FORMAT_R16G16_FLOAT);
            fullDepth = gpu->texture(c.width, c.height, DXGI_FORMAT_R32_FLOAT);
            fgReal = gpu->texture(c.width, c.height);
        }
        // Handles are returned even when a feature is unavailable: the bridge can
        // still be validated, and no DLL load is represented as feature success.
        for (unsigned i = 0; i < SlotCount; ++i)
        {
            auto &s = resources[i];
            reply.resources[i] = {
                exportHandle(s.input.resource.Get()), exportHandle(s.real.resource.Get()),
                exportHandle(s.generated.resource.Get()), exportHandle(s.fence.Get())};
        }
        reply.activeFlags = (nr ? Neural : 0) | (fg ? FrameGeneration : 0) | (c.flags & BridgeTest);
        std::string message = "GPU bridge ready";
        if (!nrError.empty())
            message += "; NR: " + nrError;
        if (!fgError.empty())
            message += "; FG: " + fgError;
        std::snprintf(reply.message, sizeof reply.message, "%s", message.c_str());
        return reply;
    }
    bool proxyRouteConfirmed() const
    {
        if (!proxy)
            return true; // Official path is validated by output, not proxy logs.
        const std::string pid = std::to_string(GetCurrentProcessId());
        bool active = false, backend = false;
        std::error_code ec;
        fs::path base = runtimeDir / L"sm86-proxy";
        for (fs::recursive_directory_iterator i(base, ec), end; i != end && !ec; i.increment(ec))
        {
            if (!i->is_regular_file() ||
                i->path().filename().string().find(pid) == std::string::npos ||
                i->path().extension() != L".jsonl")
                continue;
            std::ifstream f(i->path());
            std::string line;
            while (std::getline(f, line))
            {
                if ((line.find("\"event\":\"install\"") != std::string::npos ||
                     line.find("\"event\": \"install\"") != std::string::npos) &&
                    (line.find("\"active\":true") != std::string::npos ||
                     line.find("\"active\": true") != std::string::npos))
                    active = true;
                if (line.find("backend_install") != std::string::npos &&
                    (line.find("\"status\":0") != std::string::npos ||
                     line.find("\"status\": 0") != std::string::npos))
                    backend = true;
            }
        }
        return active && backend;
    }
    Reply process(const Command &c)
    {
        if (c.width != config.width || c.height != config.height || c.revision != config.revision ||
            (c.flags & ~(Reset | Fsr)) != (config.flags & ~(Reset | Fsr)))
            throw std::runtime_error("Stale frame/configuration revision");
        if (c.frameId <= lastFrame)
            throw std::runtime_error("Frame already processed or out of order");
        lastFrame = c.frameId;
        auto &s = resources.at(c.slot);
        if (c.fenceValue <= s.lastValue)
            throw std::runtime_error("Non-monotonic shared fence");
        s.lastValue = c.fenceValue + 1;
        LARGE_INTEGER begin, end, freq;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&begin);
        check(gpu->queue->Wait(s.fence.Get(), c.fenceValue), "Wait for Vulkan producer");
        gpu->wait();
        bool reset = (c.flags & Reset) || !history;
        if (optical)
        {
            gpu->begin();
            gpu->run("scaleMain", &s.input, thumbnail);
            gpu->submit();
            auto sample = gpu->pixels(thumbnail);
            if (!reset && sample.size() == previousThumbnail.size())
            {
                uint64_t difference = 0;
                size_t changed = 0;
                for (size_t n = 0; n < sample.size(); n += 4)
                {
                    unsigned d = 0;
                    for (unsigned k = 0; k < 3; ++k)
                        d += std::abs(int(sample[n + k]) - int(previousThumbnail[n + k]));
                    difference += d;
                    if (d > 180)
                        ++changed;
                }
                reset = difference > sample.size() / 4 * 3 * 64 && changed > sample.size() / 8;
                if (reset)
                    std::fprintf(stderr, "[RTX][Worker] scene cut: reset temporal history\n");
            }
            previousThumbnail = std::move(sample);
        }
        LARGE_INTEGER flowBegin, flowEnd;
        QueryPerformanceCounter(&flowBegin);
        if (optical)
        {
            if (!history)
                std::fprintf(stderr, "[RTX][Worker] first optical-flow dispatch\n");
            optical->execute(*gpu, s.input, reset);
            if (!history)
                std::fprintf(stderr, "[RTX][Worker] optical-flow completed\n");
        }
        QueryPerformanceCounter(&flowEnd);
        gpu->begin();
        for (unsigned k = 0; k < 6; ++k)
            gpu->mark(k);
        Texture *processed = &s.input;
        uint32_t active = c.flags & BridgeTest;
        if (nr)
        {
            gpu->run("scaleMain", &s.input, reduced);
            gpu->run("motionMain", nullptr, motion, nullptr, nullptr, &optical->vectors, 0,
                     float(reduced.width) / c.width, float(reduced.height) / c.height);
            gpu->run("zeroMain", nullptr, depth);
            gpu->transition(reduced, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            gpu->transition(motion, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            gpu->transition(depth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            gpu->transition(edited, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            nr->resource("DLSSNR.Color", reduced);
            nr->resource("DLSSNR.Output", edited);
            nr->resource("DLSSNR.MVec", motion);
            nr->resource("DLSSNR.Depth", depth);
            auto &p = nr->parameters;
            p.Set("DLSSNR.Enabled", 1);
            p.Set("DLSSNR.Reset", reset ? 1 : 0);
            p.Set("DLSSNR.MVecScaleX", 1.0f);
            p.Set("DLSSNR.MVecScaleY", 1.0f);
            p.Set("DLSSNR.DepthInverted", 1);
            p.Set("DLSSNR.Intensity", 1.0f);
            p.Set("DLSSNR.Style", 0);
            p.Set("DLSSNR.LocalToneStrength", 1.0f);
            p.Set("DLSSNR.LocalStructureStrength", 1.0f);
            if (!history)
                std::fprintf(stderr, "[RTX][NR] first EvaluateFeature begin\n");
            gpu->mark(0);
            nr->eval(*gpu);
            gpu->mark(1);
            gpu->mark(2);
            if (!history)
                std::fprintf(stderr, "[RTX][NR] EvaluateFeature returned\n");
            active |= Neural;
            const bool upscale = reduced.width < c.width || reduced.height < c.height;
            const bool fsr = upscale && (c.flags & Fsr);
            const char *scaler = fsr ? "easuMain" : "scaleMain";
            if (upscale)
            {
                gpu->run(scaler, &reduced, upInput);
                gpu->run(scaler, &edited, upOutput);
                gpu->run("residualMain", &s.input, composite, &upOutput, &upInput);
                processed = &composite;
                if (fsr)
                    active |= Fsr;
            }
            else
                processed = &edited;
            if (fsr && c.sharpness)
            {
                gpu->run("rcasMain", processed, upOutput, nullptr, nullptr, nullptr,
                         c.sharpness / 100.0f);
                processed = &upOutput;
            }
        }
        gpu->mark(3);
        gpu->copy(*processed, s.real);
        if (fg)
        {
            gpu->run("motionMain", nullptr, fullMotion, nullptr, nullptr, &optical->vectors);
            gpu->run("zeroMain", nullptr, fullDepth);
            gpu->transition(*processed, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            gpu->transition(fullMotion, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            gpu->transition(fullDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            gpu->transition(s.generated, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            gpu->transition(fgReal, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            fg->resource("DLSSG.Backbuffer", *processed);
            fg->resource("DLSSG.HUDLess", *processed);
            fg->resource("DLSSG.MVecs", fullMotion);
            fg->resource("DLSSG.Depth", fullDepth);
            fg->resource("DLSSG.OutputInterpolated", s.generated);
            fg->resource("DLSSG.OutputReal", fgReal);
            auto &p = fg->parameters;
            p.Set("DLSSG.Reset", reset ? 1u : 0u);
            p.Set("DLSSG.MvecScaleX", 1.0f);
            p.Set("DLSSG.MvecScaleY", 1.0f);
            p.Set("DLSSG.DepthInverted", 1u);
            p.Set("DLSSG.MultiFrameCount", 1u);
            p.Set("DLSSG.MultiFrameIndex", 1u);
            p.Set("DLSSG.BackbufferFrameID", static_cast<unsigned long long>(c.frameId));
            p.Set("DLSSG.CameraMotionIncluded", 1u);
            p.Set("DLSSG.CameraNear", 0.1f);
            p.Set("DLSSG.CameraFar", 1000.0f);
            p.Set("DLSSG.CameraFOV", 1.0472f);
            p.Set("DLSSG.CameraAspectRatio", float(c.width) / c.height);
            p.Set("DLSSG.InputBackbufferSubrectBaseX", 0u);
            p.Set("DLSSG.InputBackbufferSubrectBaseY", 0u);
            p.Set("DLSSG.InputBackbufferSubrectWidth", c.width);
            p.Set("DLSSG.InputBackbufferSubrectHeight", c.height);
            p.Set("DLSSG.MvecScaleX", 1.0f / c.width);
            p.Set("DLSSG.MvecScaleY", 1.0f / c.height);
            p.Set("DLSSG.MvecInvalidValue", 3.402823466e+38f);
            p.Set("DLSSG.MvecDilated", 1u);
            p.Set("DLSSG.MvecJittered", 0u);
            p.Set("DLSSG.LinearizedDepth_Scale", 1.0f);
            p.Set("DLSSG.LinearizedDepth_NearFarPartition", 1.0f);
            p.Set("DLSSG.NoWarp", static_cast<ID3D12Resource *>(nullptr));
            float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            for (const char *key :
                 {"DLSSG.CameraViewToClip", "DLSSG.ClipToCameraView", "DLSSG.ClipToPrevClip",
                  "DLSSG.PrevClipToClip", "DLSSG.ClipToLensClip"})
                p.Set(key, static_cast<void *>(identity));
            for (const char *key : {"DLSSG.CameraPosX", "DLSSG.CameraPosY", "DLSSG.CameraPosZ",
                                    "DLSSG.CameraRightY", "DLSSG.CameraRightZ", "DLSSG.CameraUpX",
                                    "DLSSG.CameraUpZ", "DLSSG.CameraFwdX", "DLSSG.CameraFwdY"})
                p.Set(key, 0.0f);
            for (const char *key : {"DLSSG.CameraRightX", "DLSSG.CameraUpY", "DLSSG.CameraFwdZ"})
                p.Set(key, 1.0f);
            p.Set("DLSSG.OutputDisableInterpolation", static_cast<ID3D12Resource *>(nullptr));
            p.Set("DLSSG.UI", static_cast<ID3D12Resource *>(nullptr));
            p.Set("DLSSG.UIAlpha", static_cast<ID3D12Resource *>(nullptr));
            p.Set("DLSSG.BidirectionalDistortionField", static_cast<ID3D12Resource *>(nullptr));
            p.Set("DLSSG.CmdQueue", static_cast<void *>(gpu->queue.Get()));
            p.Set("DLSSG.CmdAlloc", static_cast<void *>(gpu->allocator.Get()));
            p.Set("DLSSG.FenceEvent", gpu->event.h);
            if (!history)
                std::fprintf(stderr, "[RTX][FG] first EvaluateFeature begin\n");
            gpu->mark(4);
            fg->eval(*gpu);
            gpu->mark(5);
            if (!history)
                std::fprintf(stderr, "[RTX][FG] EvaluateFeature returned\n");
        }
        gpu->transition(s.input, D3D12_RESOURCE_STATE_COMMON);
        gpu->transition(s.real, D3D12_RESOURCE_STATE_COMMON);
        gpu->transition(s.generated, D3D12_RESOURCE_STATE_COMMON);
        gpu->resolveTimers();
        gpu->submit();
        auto stageTimes = gpu->timings();
        if (reset)
            fgVerified = false;
        if (fg && !fgVerified)
        {
            auto real = gpu->pixels(s.real);
            if (!reset && previousRealPixels.size() == real.size() && proxyRouteConfirmed())
            {
                auto generated = gpu->pixels(s.generated);
                size_t differentCurrent = 0, differentPrevious = 0, nonBlack = 0, samples = 0;
                for (size_t n = 0; n < real.size(); n += 64)
                {
                    ++samples;
                    unsigned dc = 0, dp = 0, brightness = 0;
                    for (size_t k = 0; k < 3; ++k)
                    {
                        dc += std::abs(int(real[n + k]) - int(generated[n + k]));
                        dp += std::abs(int(previousRealPixels[n + k]) - int(generated[n + k]));
                        brightness += generated[n + k];
                    }
                    if (dc > 6)
                        ++differentCurrent;
                    if (dp > 6)
                        ++differentPrevious;
                    if (brightness > 12)
                        ++nonBlack;
                }
                fgVerified = differentCurrent > samples / 1000 &&
                             differentPrevious > samples / 1000 && nonBlack > samples / 100;
                if (fgVerified)
                    std::fprintf(stderr, "[RTX][FG] output differs from both real frames; nonblank; "
                                         "proxy route confirmed; awaiting presentation\n");
            }
            previousRealPixels = std::move(real);
            gpu->begin();
            gpu->transition(s.generated, D3D12_RESOURCE_STATE_COMMON);
            gpu->transition(s.real, D3D12_RESOURCE_STATE_COMMON);
            gpu->submit();
        }
        if (fgVerified && !reset)
            active |= FrameGeneration;
        check(gpu->queue->Signal(s.fence.Get(), c.fenceValue + 1), "Signal Vulkan consumer");
        check(s.fence->SetEventOnCompletion(c.fenceValue + 1, gpu->event.h),
              "Complete shared consumer fence");
        if (WaitForSingleObject(gpu->event.h, 10000) != WAIT_OBJECT_0)
            throw std::runtime_error("Shared consumer fence timed out");
        history = true;
        QueryPerformanceCounter(&end);
        Reply r;
        r.result = Result::Complete;
        r.slot = c.slot;
        r.frameId = c.frameId;
        r.timestampUs = c.timestampUs;
        r.revision = c.revision;
        r.fenceValue = c.fenceValue + 1;
        r.activeFlags = active;
        r.width = nrExtent(c.width, c.height, c.percent).width;
        r.height = nrExtent(c.width, c.height, c.percent).height;
        r.flowTimeUs = static_cast<uint64_t>((flowEnd.QuadPart - flowBegin.QuadPart) * 1000000 /
                                             freq.QuadPart);
        r.nrGpuUs = stageTimes[0];
        r.reconstructionGpuUs = stageTimes[1];
        r.fgGpuUs = stageTimes[2];
        r.vramBytes = gpu->vram();
        r.workerTimeUs =
            static_cast<uint64_t>((end.QuadPart - begin.QuadPart) * 1000000 / freq.QuadPart);
        std::string msg = nr ? "NR active" : "NR off";
        if (!nrError.empty())
            msg = "NR unavailable: " + nrError;
        msg += active & Fsr ? "; FSR active" : "; FSR off";
        msg += active & FrameGeneration ? "; FG output verified"
               : fg                     ? "; FG awaiting output/route verification"
                                        : "; FG off";
        if (!fgError.empty())
            msg += "; FG unavailable: " + fgError;
        std::snprintf(r.message, sizeof r.message, "%s", msg.c_str());
        return r;
    }
};

static bool readExact(HANDLE pipe, void *data, DWORD size)
{
    auto *p = static_cast<uint8_t *>(data);
    while (size)
    {
        DWORD n = 0;
        if (!ReadFile(pipe, p, size, &n, nullptr) || !n)
            return false;
        p += n;
        size -= n;
    }
    return true;
}
static bool writeExact(HANDLE pipe, const void *data, DWORD size)
{
    auto *p = static_cast<const uint8_t *>(data);
    while (size)
    {
        DWORD n = 0;
        if (!WriteFile(pipe, p, size, &n, nullptr) || !n)
            return false;
        p += n;
        size -= n;
    }
    return true;
}

int wmain(int argc, wchar_t **argv)
{
    ChiakiCrash::installWorkerFromEnvironment();
    AddVectoredExceptionHandler(
        1,
        [](PEXCEPTION_POINTERS e) -> LONG
        {
            const auto code = e->ExceptionRecord->ExceptionCode;
            if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION)
            {
                MEMORY_BASIC_INFORMATION info{};
                VirtualQuery(e->ExceptionRecord->ExceptionAddress, &info, sizeof info);
                wchar_t module[32768] = {};
                GetModuleFileNameW(static_cast<HMODULE>(info.AllocationBase), module, 32768);
                std::fprintf(stderr, "[RTX][Crash] native exception 0x%08lx at %ls+0x%llx\n", code,
                             module,
                             static_cast<unsigned long long>(
                                 reinterpret_cast<uintptr_t>(e->ExceptionRecord->ExceptionAddress) -
                                 reinterpret_cast<uintptr_t>(info.AllocationBase)));
                std::fprintf(
                    stderr,
                    "[RTX][Crash] address=0x%llx rcx=0x%llx rdx=0x%llx r8=0x%llx r9=0x%llx\n",
                    e->ExceptionRecord->NumberParameters > 1
                        ? e->ExceptionRecord->ExceptionInformation[1]
                        : 0,
                    e->ContextRecord->Rcx, e->ContextRecord->Rdx, e->ContextRecord->R8,
                    e->ContextRecord->R9);
            }
            return EXCEPTION_CONTINUE_SEARCH;
        });
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    fs::path runtimeDir;
    bool selftest = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::wcscmp(argv[i], L"--runtime-dir") == 0 && i + 1 < argc)
            runtimeDir = fs::absolute(argv[++i]);
        else if (std::wcscmp(argv[i], L"--self-test") == 0)
            selftest = true;
        else
        {
            std::fprintf(stderr, "[RTX][Error] Unknown argument\n");
            return 2;
        }
    }
    if (runtimeDir.empty())
    {
        wchar_t exe[32768];
        GetModuleFileNameW(nullptr, exe, 32768);
        runtimeDir = fs::path(exe).parent_path() / L"runtimes";
    }
    try
    {
        if (selftest)
        {
            Gpu gpu(0);
            auto in = gpu.texture(64, 64), out = gpu.texture(64, 64),
                 residual = gpu.texture(64, 64);
            gpu.begin();
            gpu.run("zeroMain", nullptr, in);
            gpu.run("scaleMain", &in, out);
            gpu.run("residualMain", &in, residual, &out, &out);
            gpu.submit();
            auto a = gpu.pixels(in), b = gpu.pixels(residual);
            for (size_t i = 0; i < a.size(); ++i)
                if (i % 4 != 3 && a[i] != b[i])
                    throw std::runtime_error("Identity residual changed pixels");
            std::fprintf(
                stderr,
                "[RTX][SelfTest] PASS: D3D12 shader compilation, dispatch, readback and identity residual\n");
            return 0;
        }
        std::unique_ptr<Worker> worker;
        Command c;
        while (readExact(GetStdHandle(STD_INPUT_HANDLE), &c, sizeof c))
        {
            if (!valid(c))
                throw std::runtime_error("Invalid enhancement IPC command");
            if (c.operation == Operation::Shutdown)
                break;
            Reply r;
            if (c.operation == Operation::Configure)
            {
                if (worker)
                    throw std::runtime_error("Worker already configured");
                worker.reset(new Worker);
                r = worker->configure(c, runtimeDir);
            }
            else
            {
                if (!worker)
                    throw std::runtime_error("Frame before configuration");
                r = worker->process(c);
            }
            if (!writeExact(GetStdHandle(STD_OUTPUT_HANDLE), &r, sizeof r))
                break;
        }
    }
    catch (const std::exception &e)
    {
        Reply r;
        std::snprintf(r.message, sizeof r.message, "%s", e.what());
        writeExact(GetStdHandle(STD_OUTPUT_HANDLE), &r, sizeof r);
        std::fprintf(stderr, "[RTX][Worker] %s\n", e.what());
        return 1;
    }
    return 0;
}
