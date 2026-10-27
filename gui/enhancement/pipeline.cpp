// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "pipeline.h"
#include "runtime_manifest.h"
#include "runtime_route.h"
#include "worker_log.h"
#include <QCoreApplication>
#include <QProcess>
#include <QTimer>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QDebug>
#include <array>
#include <cstring>
#include <cmath>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <vulkan/vulkan_win32.h>
#endif

using namespace chiaki_enhancement;

struct EnhancementPipeline::Impl
{
    EnhancementPipeline *q;
    QProcess process;
    QTimer resizeTimer, watchdog, realTimer;
    QByteArray received;
    WorkerLogBuffer workerLogs;
    void readWorkerErrors(bool flush = false) {
        for (const auto &line : workerLogs.append(process.isOpen() ? process.readAllStandardError() : QByteArray(), flush))
            qInfo().noquote() << line;
    }
    pl_vulkan vk = nullptr;
    Extent native{}, work{};
    EnhancementOptions options;
    Command config{}, pending{};
    uint64_t revision = 0, lastSubmitted = 0, lastTimestamp = 0, lastPresented = 0,
             lastCompleted = 0;
    int submitSlot = -1, outputSlot = -1;
    bool hdr = false;
    bool applying = false;
    void setApplying(bool value) {
        if (applying == value) return;
        applying = value;
        qInfo() << "[RTX][Apply] applying=" << value;
        emit q->applyingChanged(value);
    }
    bool ready = false, inFlight = false, failed = false, generated = false, fgPresented = false;
    bool generatedFramePresented = false, realFramePresented = true;
    uint64_t realCount = 0, generatedCount = 0, previousRealCount = 0, previousGeneratedCount = 0;
    QElapsedTimer cadenceClock, debugClock;
    void reportCadence() {
        const qint64 ms = cadenceClock.elapsed();
        if (ms < 1000) return;
        const double realFps = (realCount - previousRealCount) * 1000.0 / ms;
        const double generatedFps = (generatedCount - previousGeneratedCount) * 1000.0 / ms;
        emit q->cadenceChanged(QStringLiteral("Submitted: %1 real + %2 generated = %3 fps\nTotals: %4 real · %5 generated")
            .arg(realFps, 0, 'f', 1).arg(generatedFps, 0, 'f', 1).arg(realFps + generatedFps, 0, 'f', 1)
            .arg(realCount).arg(generatedCount));
        qInfo().nospace() << "[RTX][Present] real=" << realCount << " generated=" << generatedCount
            << " real_fps=" << realFps << " generated_fps=" << generatedFps;
        previousRealCount = realCount; previousGeneratedCount = generatedCount;
        cadenceClock.restart();
    }
    int halfIntervalMs = 8;
    uint64_t sourceIntervalUs = 0, presentationCostUs = 0;
    bool presentationLimited = false;
    uint32_t capabilities = 0, active = 0;
    QString status;
#ifdef Q_OS_WIN
    struct Image
    {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        pl_tex texture = nullptr;
        bool held = true;
    };
    struct Slot
    {
        Image input, real, generated;
        VkSemaphore fence = VK_NULL_HANDLE;
        uint64_t value = 0;
    };
    std::array<Slot, SlotCount> resources{};
    PFN_vkGetDeviceProcAddr deviceProc = nullptr;
    PFN_vkGetPhysicalDeviceProperties2 properties = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties memoryProperties = nullptr;
    PFN_vkGetPhysicalDeviceImageFormatProperties2 imageProperties = nullptr;
    PFN_vkCreateImage createImage = nullptr;
    PFN_vkDestroyImage destroyImage = nullptr;
    PFN_vkGetImageMemoryRequirements getRequirements = nullptr;
    PFN_vkAllocateMemory allocateMemory = nullptr;
    PFN_vkFreeMemory freeMemory = nullptr;
    PFN_vkBindImageMemory bindMemory = nullptr;
    PFN_vkCreateSemaphore createSemaphore = nullptr;
    PFN_vkDestroySemaphore destroySemaphore = nullptr;
    PFN_vkImportSemaphoreWin32HandleKHR importSemaphore = nullptr;
#endif
    explicit Impl(EnhancementPipeline *owner) : q(owner)
    {
        cadenceClock.start();
        debugClock.start();
        resizeTimer.setSingleShot(true);
        resizeTimer.setInterval(180);
        watchdog.setSingleShot(true);
        watchdog.setInterval(15000);
        realTimer.setSingleShot(true);
        realTimer.setTimerType(Qt::PreciseTimer);
        QObject::connect(&resizeTimer, &QTimer::timeout, q, [this] { start(); });
        QObject::connect(
            &watchdog, &QTimer::timeout, q, [this]
            { fail(QStringLiteral("Enhancement worker timed out; ordinary playback restored")); });
        QObject::connect(&realTimer, &QTimer::timeout, q,
                         [this]
                         {
                             generated = false;
                             emit q->frameAvailable();
                         });
        QObject::connect(&process, &QProcess::started, q,
                         [this]
                         {
                             send(config);
                             watchdog.start();
                         });
        QObject::connect(&process, &QProcess::readyReadStandardOutput, q,
                         [this] { readReplies(); });
        QObject::connect(
            &process, &QProcess::readyReadStandardError, q, [this]
            { readWorkerErrors(); });
        QObject::connect(&process, &QProcess::errorOccurred, q,
                         [this](QProcess::ProcessError e)
                         {
                             if (e != QProcess::Crashed || !failed)
                                 fail(QStringLiteral("Enhancement worker: ") +
                                      process.errorString());
                         });
        QObject::connect(
            &process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), q,
            [this](int exitCode, QProcess::ExitStatus exitStatus)
            {
                readWorkerErrors(true);
                qInfo() << "[RTX][Worker] exit_code=" << exitCode << "exit_status=" << int(exitStatus);
                if (!failed && ready)
                    fail(QStringLiteral("Enhancement worker exited; ordinary playback restored"));
            });
    }
    void report(const QString &message)
    {
        if (status != message) qInfo().noquote() << "[RTX][Status]" << message;
        status = message;
        emit q->statusChanged(message, int(native.width), int(native.height), int(work.width),
                              int(work.height), bool(active & Neural), bool(active & Fsr),
                              fgPresented && !presentationLimited);
    }
    bool requested() const
    {
        return options.neural || options.frameGeneration || options.bridgeTest;
    }
    void send(const Command &c)
    {
        if (process.write(reinterpret_cast<const char *>(&c), sizeof c) != sizeof c)
            fail(QStringLiteral("Enhancement IPC write failed"));
    }
    void stop(bool preserveApplying = false)
    {
        if (!preserveApplying) setApplying(false);
        resizeTimer.stop();
        watchdog.stop();
        realTimer.stop();
        ready = false;
        inFlight = false;
        active = 0;
        capabilities = 0;
        fgPresented = false;
        presentationCostUs = 0;
        presentationLimited = false;
        generatedFramePresented = false;
        realFramePresented = true;
        realCount = generatedCount = previousRealCount = previousGeneratedCount = 0;
        cadenceClock.restart();
        generated = false;
        outputSlot = -1;
        submitSlot = -1;
        // An unsuccessful worker must never leave Vulkan waiting on an unsignaled
        // fence. Output textures are released only after a completed reply.
        process.blockSignals(true);
        if (process.state() != QProcess::NotRunning)
        {
            process.kill();
            process.waitForFinished(1000);
            qInfo() << "[RTX][Worker] requested_stop=true exit_code=" << process.exitCode()
                    << "exit_status=" << int(process.exitStatus());
        }
        process.blockSignals(false);
        if (process.isOpen()) {
            process.readAllStandardOutput();
        }
        readWorkerErrors(true);
        received.clear();
        pending = {};
        emit q->cadenceChanged(QStringLiteral("No enhanced frames presented"));
#ifdef Q_OS_WIN
        if (vk && destroySemaphore)
        {
            if (qEnvironmentVariableIsSet("CHIAKI_ENHANCEMENT_CLEANUP_TRACE"))
                qInfo() << "[RTX][Cleanup] finish before resources";
            pl_gpu_finish(vk->gpu);
            if (qEnvironmentVariableIsSet("CHIAKI_ENHANCEMENT_CLEANUP_TRACE"))
                qInfo() << "[RTX][Cleanup] GPU finished";
            for (auto &s : resources)
            {
                for (Image *i : {&s.input, &s.real, &s.generated})
                {
                    if (i->texture && !i->held)
                    {
                        VkSemaphore sem = VK_NULL_HANDLE;
                        VkSemaphoreCreateInfo ci{};
                        ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
                        if (createSemaphore(vk->device, &ci, nullptr, &sem) == VK_SUCCESS)
                        {
                            pl_vulkan_hold_params hp{};
                            hp.tex = i->texture;
                            hp.layout = VK_IMAGE_LAYOUT_GENERAL;
                            hp.qf = VK_QUEUE_FAMILY_EXTERNAL;
                            hp.semaphore.sem = sem;
                            pl_vulkan_hold_ex(vk->gpu, &hp);
                            pl_gpu_finish(vk->gpu);
                            destroySemaphore(vk->device, sem, nullptr);
                        }
                    }
                    if (i->texture)
                        pl_tex_destroy(vk->gpu, &i->texture);
                    if (i->image)
                        destroyImage(vk->device, i->image, nullptr);
                    if (i->memory)
                        freeMemory(vk->device, i->memory, nullptr);
                    *i = Image{};
                }
                if (s.fence)
                    destroySemaphore(vk->device, s.fence, nullptr);
                s = Slot{};
            }
        }
#endif
        if (qEnvironmentVariableIsSet("CHIAKI_ENHANCEMENT_CLEANUP_TRACE"))
            qInfo() << "[RTX][Cleanup] resources released";
        lastSubmitted = 0;
        lastTimestamp = 0;
        lastPresented = 0;
        lastCompleted = 0;
    }
    void fail(const QString &reason, bool unsupportedAdapter = false)
    {
        if (failed)
            return;
        failed = true;
        qWarning().noquote() << "[RTX][Error]" << reason
            << "Report at https://github.com/TheHiepDang/chiaki-ngx/issues/new; attach the matching chiaki_ngx_*.log and chiaki_session_*.log from Settings > General > Log Directory.";
        stop();
        report(reason);
        if (options.neural && unsupportedAdapter) emit q->neuralUnavailable(reason);
        emit q->frameAvailable();
    }
#ifdef Q_OS_WIN
    bool functions()
    {
        deviceProc = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
            vk->get_proc_addr(vk->instance, "vkGetDeviceProcAddr"));
        properties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
            vk->get_proc_addr(vk->instance, "vkGetPhysicalDeviceProperties2"));
        memoryProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
            vk->get_proc_addr(vk->instance, "vkGetPhysicalDeviceMemoryProperties"));
        imageProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceImageFormatProperties2>(
            vk->get_proc_addr(vk->instance, "vkGetPhysicalDeviceImageFormatProperties2"));
        if (!deviceProc || !properties || !memoryProperties || !imageProperties)
            return false;
#define GET(name, field)                                                                           \
    field = reinterpret_cast<PFN_##name>(deviceProc(vk->device, #name));                           \
    if (!field)                                                                                    \
    return false
        GET(vkCreateImage, createImage);
        GET(vkDestroyImage, destroyImage);
        GET(vkGetImageMemoryRequirements, getRequirements);
        GET(vkAllocateMemory, allocateMemory);
        GET(vkFreeMemory, freeMemory);
        GET(vkBindImageMemory, bindMemory);
        GET(vkCreateSemaphore, createSemaphore);
        GET(vkDestroySemaphore, destroySemaphore);
        GET(vkImportSemaphoreWin32HandleKHR, importSemaphore);
#undef GET
        return true;
    }
    bool importImage(Image &out, HANDLE handle)
    {
        VkExternalMemoryImageCreateInfo external{};
        external.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
        external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
        std::vector<uint32_t> families;
        for (uint32_t f :
             {vk->queue_graphics.index, vk->queue_compute.index, vk->queue_transfer.index})
            if (std::find(families.begin(), families.end(), f) == families.end())
                families.push_back(f);
        VkImageCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.pNext = &external;
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.format = VK_FORMAT_R8G8B8A8_UNORM;
        ci.extent = {native.width, native.height, 1};
        ci.mipLevels = 1;
        ci.arrayLayers = 1;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                   VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                   VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ci.sharingMode =
            families.size() > 1 ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE;
        ci.queueFamilyIndexCount = uint32_t(families.size());
        ci.pQueueFamilyIndices = families.data();
        if (createImage(vk->device, &ci, nullptr, &out.image) != VK_SUCCESS)
            return false;
        VkMemoryRequirements requirements{};
        getRequirements(vk->device, out.image, &requirements);
        VkPhysicalDeviceMemoryProperties memory{};
        memoryProperties(vk->phys_device, &memory);
        uint32_t index = memory.memoryTypeCount;
        for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
            if ((requirements.memoryTypeBits & (1u << i)) &&
                (memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            {
                index = i;
                break;
            }
        if (index == memory.memoryTypeCount)
            return false;
        VkImportMemoryWin32HandleInfoKHR import{};
        import.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR;
        import.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
        import.handle = handle;
        VkMemoryDedicatedAllocateInfo dedicated{};
        dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
        dedicated.pNext = &import;
        dedicated.image = out.image;
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.pNext = &dedicated;
        ai.allocationSize = requirements.size;
        ai.memoryTypeIndex = index;
        if (allocateMemory(vk->device, &ai, nullptr, &out.memory) != VK_SUCCESS)
            return false;
        if (bindMemory(vk->device, out.image, out.memory, 0) != VK_SUCCESS)
            return false;
        pl_vulkan_wrap_params wp{};
        wp.image = out.image;
        wp.width = int(native.width);
        wp.height = int(native.height);
        wp.format = ci.format;
        wp.usage = ci.usage;
        out.texture = pl_vulkan_wrap(vk->gpu, &wp);
        return out.texture;
    }
    bool importSlots(const Reply &r)
    {
        HANDLE worker = OpenProcess(PROCESS_DUP_HANDLE, FALSE, DWORD(process.processId()));
        if (!worker)
            return false;
        bool okay = true;
        auto duplicate = [&](uint64_t source) -> HANDLE
        {
            HANDLE copy = nullptr;
            if (!DuplicateHandle(worker, reinterpret_cast<HANDLE>(source), GetCurrentProcess(),
                                 &copy, 0, FALSE, DUPLICATE_SAME_ACCESS))
                okay = false;
            return copy;
        };
        // Close every received handle, even if an earlier import failed.
        for (unsigned i = 0; i < SlotCount; ++i)
        {
            auto &s = resources[i];
            const uint64_t handles[] = {r.resources[i].input, r.resources[i].real,
                                        r.resources[i].generated};
            Image *images[] = {&s.input, &s.real, &s.generated};
            for (unsigned j = 0; j < 3; ++j)
            {
                HANDLE h = duplicate(handles[j]);
                if (okay)
                    okay = importImage(*images[j], h);
                if (h)
                    CloseHandle(h);
            }
            HANDLE h = duplicate(r.resources[i].fence);
            if (okay)
            {
                VkSemaphoreTypeCreateInfo type{};
                type.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
                type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
                VkSemaphoreCreateInfo ci{};
                ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
                ci.pNext = &type;
                okay = createSemaphore(vk->device, &ci, nullptr, &s.fence) == VK_SUCCESS;
                if (okay)
                {
                    VkImportSemaphoreWin32HandleInfoKHR si{};
                    si.sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR;
                    si.semaphore = s.fence;
                    si.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT;
                    si.handle = h;
                    okay = importSemaphore(vk->device, &si) == VK_SUCCESS;
                }
            }
            if (h)
                CloseHandle(h);
        }
        CloseHandle(worker);
        return okay;
    }
    bool hold(Image &i, Slot &s, uint64_t value)
    {
        if (i.held)
            return true;
        pl_vulkan_hold_params p{};
        p.tex = i.texture;
        p.layout = VK_IMAGE_LAYOUT_GENERAL;
        p.qf = VK_QUEUE_FAMILY_EXTERNAL;
        p.semaphore = {s.fence, value};
        if (!pl_vulkan_hold_ex(vk->gpu, &p))
            return false;
        i.held = true;
        return true;
    }
    bool holdLocal(Image &i)
    {
        if (i.held)
            return true;
        VkSemaphore sem = VK_NULL_HANDLE;
        VkSemaphoreCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        if (createSemaphore(vk->device, &ci, nullptr, &sem) != VK_SUCCESS)
            return false;
        pl_vulkan_hold_params p{};
        p.tex = i.texture;
        p.layout = VK_IMAGE_LAYOUT_GENERAL;
        p.qf = VK_QUEUE_FAMILY_EXTERNAL;
        p.semaphore.sem = sem;
        bool okay = pl_vulkan_hold_ex(vk->gpu, &p);
        pl_gpu_finish(vk->gpu);
        destroySemaphore(vk->device, sem, nullptr);
        if (okay)
            i.held = true;
        return okay;
    }
    void release(Image &i, Slot &s, uint64_t value)
    {
        if (!i.held)
            return;
        pl_vulkan_release_params p{};
        p.tex = i.texture;
        p.layout = VK_IMAGE_LAYOUT_GENERAL;
        p.qf = VK_QUEUE_FAMILY_EXTERNAL;
        if (value)
            p.semaphore = {s.fence, value};
        pl_vulkan_release_ex(vk->gpu, &p);
        i.held = false;
    }
#endif
    void start()
    {
        if (!vk || !requested() || failed)
            return;
#ifndef Q_OS_WIN
        fail(QStringLiteral("Enhancements require Windows x64"));
#else
        if (!functions())
        {
            fail(QStringLiteral("Required Vulkan/D3D12 sharing functions unavailable"));
            return;
        }
        VkPhysicalDeviceIDProperties id{};
        id.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
        VkPhysicalDeviceProperties2 p{};
        p.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        p.pNext = &id;
        properties(vk->phys_device, &p);
        if ((options.neural || options.frameGeneration) &&
            !isRtxAdapter(p.properties.vendorID, QString::fromUtf8(p.properties.deviceName).toStdWString())) {
            fail(QStringLiteral("RTX enhancements require an NVIDIA RTX GPU; active adapter: %1")
                .arg(QString::fromUtf8(p.properties.deviceName)), options.neural);
            return;
        }
        if (!id.deviceLUIDValid)
        {
            fail(QStringLiteral("Vulkan adapter has no Windows LUID"));
            return;
        }
        QString executable = qEnvironmentVariable("CHIAKI_ENHANCEMENT_WORKER");
        if (executable.isEmpty())
            executable =
                QCoreApplication::applicationDirPath() + QStringLiteral("/enhancement/nvngx.dll");
        if (!QFileInfo::exists(executable))
        {
            fail(QStringLiteral("Enhancement worker is not installed (enhancement/nvngx.dll)"));
            return;
        }
        QString runtime = qEnvironmentVariable("CHIAKI_ENHANCEMENT_RUNTIME_DIR");
        if (runtime.isEmpty())
            runtime = QCoreApplication::applicationDirPath() + QStringLiteral("/runtimes");
        if (options.neural || options.frameGeneration)
        {
            const bool sm86 = options.frameGeneration && usesSm86(
                QString::fromUtf8(p.properties.deviceName).toStdWString());
            qInfo() << "[RTX][RuntimeRoute] sm86=" << sm86;
            const QString error = verifyRuntimeManifest(runtime, options.neural, options.frameGeneration, sm86);
            if (!error.isEmpty())
            {
                fail(error);
                return;
            }
        }
        qInfo().nospace() << "[RTX][Runtime] directory=" << runtime;
        qInfo().nospace() << "[RTX][Config] native=" << native.width << 'x' << native.height
            << " nr=" << options.neural << " percent=" << options.percent << " fsr=" << options.fsr
            << " sharpness=" << options.sharpness << " fg=" << options.frameGeneration;
        config = Command{};
        config.width = native.width;
        config.height = native.height;
        config.parentPid = GetCurrentProcessId();
        config.revision = ++revision;
        config.percent = unsigned(options.percent);
        config.sharpness = unsigned(options.sharpness);
        config.nrPassCount = options.nrPassCount;
        config.nrTuning = options.nrTuning;
        std::copy(options.additionalPasses.begin(), options.additionalPasses.end(), config.additionalPasses);
        config.flags = (options.neural ? Neural : 0) |
                       (options.frameGeneration ? FrameGeneration : 0) |
                       ((options.neural && options.fsr && canUpscale(native, work)) ? Fsr : 0) |
                       (options.bridgeTest ? BridgeTest : 0);
        qInfo() << "[RTX][NR] configured_passes=" << config.nrPassCount;
        for (unsigned n = 0; n < config.nrPassCount; ++n) {
            const auto p = passConfig(config, n);
            qInfo().noquote() << QStringLiteral("[RTX][NR][Pass%1]").arg(n + 1)
                << "percent=" << p.percent << "fsr=" << p.fsr << "sharpness=" << p.sharpness
                << "preset=" << p.tuning.style << "local_structure=" << p.tuning.localStructure
                << "local_tone=" << p.tuning.localTone << "skin_structure=" << p.tuning.skinStructure
                << "automatic_mask=" << p.tuning.autoMask << "intensity=" << p.tuning.intensity;
        }
        std::memcpy(&config.adapterLuid, id.deviceLUID, 8);
        process.setProgram(executable);
        process.setArguments({QStringLiteral("--runtime-dir"), runtime});
        process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a)
                                                  { a->flags |= CREATE_NO_WINDOW; });
        report(QStringLiteral("Starting enhancement worker…"));
        process.start();
        watchdog.start();
#endif
    }
    void readReplies()
    {
        received += process.readAllStandardOutput();
        while (received.size() >= int(sizeof(Reply)))
        {
            Reply r;
            std::memcpy(&r, received.constData(), sizeof r);
            if (r.result == Result::Complete && (active & FrameGeneration) && !realFramePresented)
                return; // Three slots allow processing overlap without replacing an unpresented pair.
            received.remove(0, sizeof r);
            if (r.magic != Magic || r.version != Version || r.size != sizeof r)
            {
                fail(QStringLiteral("Incompatible enhancement worker protocol"));
                return;
            }
            r.message[sizeof(r.message) - 1] = 0;
            if (r.revision && r.revision < config.revision)
                continue; // A retired worker/configuration cannot affect the current stream.
            if (r.result == Result::Error)
            {
                fail(QString::fromUtf8(r.message));
                return;
            }
            if (r.revision != config.revision)
            {
                fail(QStringLiteral("Enhancement worker returned a stale configuration"));
                return;
            }
            watchdog.stop();
#ifdef Q_OS_WIN
            if (r.result == Result::Ready)
            {
                if (!importSlots(r))
                {
                    fail(QStringLiteral("Vulkan could not import D3D12 shared resources"));
                    return;
                }
                capabilities = r.activeFlags;
                // Runtime/feature failures are not proof of unsupported hardware.
                // Keep the user control available for retry.
                if (!capabilities)
                {
                    fail(QString::fromUtf8(r.message));
                    return;
                }
                ready = true;
                report(QString::fromUtf8(r.message));
                emit q->frameAvailable();
            }
            else if (r.result == Result::Complete)
            {
                if (!inFlight || r.slot != pending.slot || r.frameId != pending.frameId ||
                    r.fenceValue != pending.fenceValue + 1)
                {
                    if (r.frameId <= lastCompleted) continue; // Idempotent consumption of an old completion.
                    fail(QStringLiteral("Enhancement protocol error: expected frame %1 slot %2 fence %3, received frame %4 slot %5 fence %6; ordinary playback restored, controls remain available")
                        .arg(pending.frameId).arg(pending.slot).arg(pending.fenceValue+1)
                        .arg(r.frameId).arg(r.slot).arg(r.fenceValue));
                    return;
                }
                auto &s = resources[r.slot];
                s.value = r.fenceValue;
                if (outputSlot >= 0 && outputSlot != int(r.slot))
                {
                    auto &old = resources[outputSlot];
                    // Complete all reads before this slot can become a producer.
                    if (!holdLocal(old.real) || !holdLocal(old.generated))
                    {
                        fail(QStringLiteral("Output ownership transfer failed"));
                        return;
                    }
                }
                outputSlot = int(r.slot);
                lastCompleted = r.frameId;
                release(s.real, s, r.fenceValue);
                release(s.generated, s, r.fenceValue);
                if ((r.frameId % 60) == 0)
                    qInfo().nospace()
                        << "[RTX][Worker] frame=" << r.frameId << " worker_us=" << r.workerTimeUs
                        << " flow_host_us=" << r.flowTimeUs << " nr_gpu_us=" << r.nrGpuUs
                        << " reconstruction_gpu_us=" << r.reconstructionGpuUs
                        << " fg_gpu_us=" << r.fgGpuUs << " vram_bytes=" << r.vramBytes;
                if (qEnvironmentVariableIsSet("CHIAKI_ENHANCEMENT_TRACE_PACING"))
                    qInfo() << "[RTX][Pace] complete" << r.frameId << debugClock.elapsed();
                inFlight = false;
                active = r.activeFlags;
                setApplying(false); // Unlock only after the first valid processed frame.
                // Some Windows presentation paths still block at desktop refresh
                // even with IMMEDIATE requested. Do not spend two display slots
                // per source frame if that would halve the real cadence.
                const uint64_t realBudgetUs = std::max(sourceIntervalUs, r.workerTimeUs);
                if (sourceIntervalUs && presentationCostUs && (active & FrameGeneration)) {
                    if (presentationCostUs * 10 > realBudgetUs * 6)
                        presentationLimited = true;
                    else if (presentationCostUs * 10 < realBudgetUs * 4)
                        presentationLimited = false;
                }
                generated = bool(active & FrameGeneration) && !presentationLimited;
                generatedFramePresented = false;
                realFramePresented = false;
                realTimer.stop();
                if (generated)
                {
                    const uint64_t interval = lastTimestamp && r.timestampUs > lastTimestamp
                                                  ? r.timestampUs - lastTimestamp
                                                  : 16667;
                    halfIntervalMs =
                        int(std::max<uint64_t>(1, std::min<uint64_t>(50, interval / 2000)));
                }
                lastTimestamp = r.timestampUs;
                report(QString::fromUtf8(r.message) + (presentationLimited
                    ? QStringLiteral("; FG presentation skipped to preserve real FPS") : QString()));
                emit q->frameAvailable();
            }
            else
            {
                fail(QStringLiteral("Unknown worker response"));
                return;
            }
#endif
        }
    }
};

EnhancementPipeline::EnhancementPipeline(QObject *p) : QObject(p), impl(new Impl(this)) {}
EnhancementPipeline::~EnhancementPipeline()
{
    impl->stop();
}
void EnhancementPipeline::reset()
{
    impl->stop();
    impl->failed = false;
    impl->native = {};
    impl->work = {};
    impl->report(QStringLiteral("Off"));
}
void EnhancementPipeline::prepare(pl_vulkan vk, Extent native, const EnhancementOptions &options,
                                  bool hdr)
{
    auto &i = *impl;
    // Reconstruction choices do not alter resources or NGX feature state.
    // Never restart a worker with an in-flight frame just to change FSR/RCAS.
    const bool sameResources = i.hdr == hdr && i.vk == vk && i.native.width == native.width &&
        i.native.height == native.height && i.options.neural == options.neural &&
        i.options.frameGeneration == options.frameGeneration && i.options.bridgeTest == options.bridgeTest &&
        (!options.neural || (i.options.percent == options.percent && i.options.nrPassCount == options.nrPassCount &&
            (options.nrPassCount < 2 || i.options.additionalPasses[0].percent == options.additionalPasses[0].percent) &&
            (options.nrPassCount < 3 || i.options.additionalPasses[1].percent == options.additionalPasses[1].percent)));
    if (sameResources) {
        i.options = options;
        i.config.flags = (i.config.flags & ~uint32_t(Fsr)) | ((options.neural && options.fsr && canUpscale(i.native, i.work)) ? Fsr : 0);
        i.config.sharpness = uint32_t(options.sharpness);
        i.config.nrPassCount = options.nrPassCount;
        i.config.nrTuning = options.nrTuning;
        std::copy(options.additionalPasses.begin(), options.additionalPasses.end(), i.config.additionalPasses);
        return;
    }
    {
        i.setApplying(options.neural || options.frameGeneration || options.bridgeTest);
        i.stop(true);
        i.vk = vk;
        i.native = native;
        i.work = nrExtent(native.width, native.height, options.percent);
        i.options = options;
        i.hdr = hdr;
        i.failed = false;
    }
    if (!i.requested())
    {
        i.report(QStringLiteral("Off"));
        return;
    }
    if (hdr)
    {
        if (!i.failed)
            i.fail(QStringLiteral("Enhancements are SDR-only; ordinary HDR playback retained"));
        return;
    }
    if (!vk)
    {
        if (!i.failed)
            i.fail(QStringLiteral("Enhancements require the Vulkan renderer"));
        return;
    }
    if (!native.width || !native.height) {
        i.setApplying(false);
        return;
    }
    i.setApplying(true);
    i.report(QStringLiteral("Waiting for video size to settle…"));
    i.resizeTimer.start();
}
pl_tex EnhancementPipeline::beginFrame(uint64_t frameId, uint64_t timestampUs)
{
    auto &i = *impl;
#ifdef Q_OS_WIN
    if (!i.ready || i.inFlight || i.failed || frameId == i.lastSubmitted || !i.capabilities)
        return nullptr;
    for (unsigned n = 0; n < SlotCount; ++n)
        if (int(n) != i.outputSlot)
        {
            auto &s = i.resources[n];
            if (!s.input.held && !i.holdLocal(s.input))
            {
                i.fail(QStringLiteral("Input ownership transfer failed"));
                return nullptr;
            }
            i.release(s.input, s, s.value);
            i.submitSlot = int(n);
            i.pending = i.config;
            i.pending.operation = Operation::Process;
            i.pending.slot = n;
            i.pending.frameId = frameId;
            i.pending.timestampUs = timestampUs;
            i.pending.fenceValue = s.value + 1;
            if (!i.lastSubmitted || timestampUs > i.lastTimestamp + 250000)
                i.pending.flags |= Reset;
            return s.input.texture;
        }
#else
    Q_UNUSED(frameId);
    Q_UNUSED(timestampUs);
#endif
    return nullptr;
}
bool EnhancementPipeline::submitFrame()
{
    auto &i = *impl;
#ifdef Q_OS_WIN
    if (i.submitSlot < 0)
        return false;
    auto &s = i.resources[i.submitSlot];
    if (!i.hold(s.input, s, i.pending.fenceValue))
    {
        i.fail(QStringLiteral("Cannot hand frame to enhancement worker"));
        return false;
    }
    pl_gpu_flush(i.vk->gpu);
    i.inFlight = true;
    i.lastSubmitted = i.pending.frameId;
    i.send(i.pending);
    i.watchdog.start();
    i.submitSlot = -1;
    return true;
#else
    return false;
#endif
}
bool EnhancementPipeline::outputFrame(pl_frame &frame)
{
    auto &i = *impl;
#ifdef Q_OS_WIN
    if (!i.ready || i.outputSlot < 0 || !i.active)
        return false;
    frame = {};
    frame.num_planes = 1;
    frame.planes[0].texture = i.generated ? i.resources[i.outputSlot].generated.texture
                                          : i.resources[i.outputSlot].real.texture;
    frame.planes[0].components = 4;
    for (int c = 0; c < 4; ++c)
        frame.planes[0].component_mapping[c] = c;
    frame.repr = pl_color_repr_rgb;
    frame.repr.alpha = PL_ALPHA_NONE;
    frame.color = pl_color_space_srgb;
    frame.crop = {0, 0, float(i.native.width), float(i.native.height)};
    return true;
#else
    Q_UNUSED(frame);
    return false;
#endif
}
void EnhancementPipeline::notePresented(uint64_t presentationWorkUs)
{
    auto &i = *impl;
    if (qEnvironmentVariableIsSet("CHIAKI_ENHANCEMENT_TRACE_PACING"))
        qInfo() << "[RTX][Pace] present" << i.lastCompleted << (i.generated ? "gen" : "real") << i.debugClock.elapsed() << "work_us" << presentationWorkUs;
    if (i.outputSlot >= 0 && (i.generated ? !i.generatedFramePresented : !i.realFramePresented)) {
        const uint64_t cost = std::min<uint64_t>(100000, presentationWorkUs);
        i.presentationCostUs = i.presentationCostUs ? (i.presentationCostUs * 3 + cost) / 4 : cost;
    }
    if (i.generated && !i.generatedFramePresented)
    {
        i.generatedFramePresented = true;
        ++i.generatedCount;
        i.reportCadence();
        // Swapchain acquire/render/submit may already have consumed this phase's gap.
        // Never add that time again after a blocking presentation.
        i.realTimer.start(std::max(0, i.halfIntervalMs - int(std::min<uint64_t>(1000, presentationWorkUs/1000))));
        if (!i.fgPresented)
        {
            i.fgPresented = true;
            i.report(i.status + QStringLiteral("; generated frame presented"));
        }
    }
    else if (!i.generated && i.outputSlot >= 0 && !i.realFramePresented) {
        i.realFramePresented = true;
        ++i.realCount;
        i.reportCadence();
        // Defer adoption to the event loop, after this presentation has finished.
        QTimer::singleShot(0, this, [this]() { impl->readReplies(); });
    }
}
uint64_t EnhancementPipeline::realPresentedCount() const { return impl->realCount; }
uint64_t EnhancementPipeline::generatedPresentedCount() const { return impl->generatedCount; }
uint64_t EnhancementPipeline::completedFrameId() const
{
    return impl->lastCompleted;
}
bool EnhancementPipeline::frameGenerationReady() const
{
    return impl->ready && bool(impl->active & FrameGeneration);
}

bool EnhancementPipeline::generatedOutput() const { return impl->generated && bool(impl->active & FrameGeneration); }

bool EnhancementPipeline::ownsPresentation() const { return impl->ready && !impl->failed && bool(impl->capabilities); }

bool EnhancementPipeline::hasUnpresentedOutput() const { return impl->active && impl->outputSlot >= 0 && (impl->generated ? !impl->generatedFramePresented : !impl->realFramePresented); }

void EnhancementPipeline::setSourceInterval(uint64_t intervalUs)
{
    if (impl->sourceIntervalUs != intervalUs) {
        impl->sourceIntervalUs = intervalUs;
        impl->presentationCostUs = 0;
        impl->presentationLimited = false;
    }
}
