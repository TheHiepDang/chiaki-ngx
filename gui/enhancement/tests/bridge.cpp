// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "../pipeline.h"
#include "../screenshot.h"
#include <QThread>
#include <QGuiApplication>
#include <QWindow>
#include <QTimer>
#include <QElapsedTimer>
#include <QImage>
#include <QDir>
#include <limits>
#include <QDebug>
#include <vector>
#include <cstring>
#ifdef Q_OS_WIN
#include <windows.h>
#include <vulkan/vulkan_win32.h>
#endif
extern "C"
{
#include <libplacebo/log.h>
}

// Requires no console/session: exercise the actual cross-process D3D12/Vulkan
// resources with moving, asymmetric RGB patterns and compare every returned byte.
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    const bool present = app.arguments().contains("--present");
    QWindow testWindow;
    testWindow.resize(640, 360);
    auto log = pl_log_create(PL_API_VER, nullptr);
    const char *extensions[] = {"VK_KHR_external_memory_win32", "VK_KHR_external_semaphore_win32"};
    pl_vk_inst instance = nullptr;
    if (present)
    {
        const char *instanceExtensions[] = {"VK_KHR_surface", "VK_KHR_win32_surface"};
        pl_vk_inst_params params{};
        params.extensions = instanceExtensions;
        params.num_extensions = 2;
        instance = pl_vk_inst_create(log, &params);
        if (!instance)
            return 1;
        testWindow.create();
    }
    pl_vulkan_params p{};
    p.async_compute = true;
    p.async_transfer = true;
    p.opt_extensions = extensions;
    p.num_opt_extensions = 2;
    if (instance)
    {
        p.instance = instance->instance;
        p.get_proc_addr = instance->get_proc_addr;
    }
    auto vk = pl_vulkan_create(log, &p);
    if (!vk)
    {
        qCritical() << "Cannot create Vulkan device";
        return 1;
    }
    pl_swapchain swapchain = nullptr;
    pl_renderer renderer = pl_renderer_create(log, vk->gpu);
    pl_tex sourceTexture = nullptr;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
#ifdef Q_OS_WIN
    if (present)
    {
        auto create = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(
            instance->get_proc_addr(instance->instance, "vkCreateWin32SurfaceKHR"));
        VkWin32SurfaceCreateInfoKHR ci{};
        ci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        ci.hinstance = GetModuleHandleW(nullptr);
        ci.hwnd = reinterpret_cast<HWND>(testWindow.winId());
        if (!create || create(instance->instance, &ci, nullptr, &surface) != VK_SUCCESS)
            return 1;
        pl_vulkan_swapchain_params sp{};
        sp.surface = surface;
        sp.present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        swapchain = pl_vulkan_create_swapchain(vk, &sp);
        if (!swapchain)
            return 1;
        int w = 640, h = 360;
        if (!pl_swapchain_resize(swapchain, &w, &h))
            return 1;
    }
#endif
    int result = 1, completed = 0, presented = 0;
    uint64_t id = 0;
    {
        EnhancementPipeline pipeline;
        EnhancementOptions options;
        options.bridgeTest = true;
        if (app.arguments().contains("--nr"))
        {
            options.neural = true;
            options.sharpness = 0;
        }
        if (app.arguments().contains("--fg"))
            options.frameGeneration = true;
        if (app.arguments().contains("--no-fsr"))
            options.fsr = false;
        const auto argument = [&](const QString &name, int fallback)
        {
            int at = app.arguments().indexOf(name);
            if (at >= 0 && at + 1 < app.arguments().size())
            {
                bool okay = false;
                int value = app.arguments()[at + 1].toInt(&okay);
                if (okay && value > 0)
                    return value;
            }
            return fallback;
        };
        const auto tuningArgument = [&](const QString &name, float fallback) {
            const int at = app.arguments().indexOf(name);
            bool okay = false;
            const float value = at >= 0 && at + 1 < app.arguments().size() ? app.arguments()[at + 1].toFloat(&okay) : fallback;
            return okay && std::isfinite(value) ? value : fallback;
        };
        const auto tuningFor = [&](const QString &prefix, chiaki_enhancement::NrTuning fallback) {
            fallback.intensity = tuningArgument(prefix + "intensity", fallback.intensity);
            fallback.style = unsigned(tuningArgument(prefix + "style", float(fallback.style)));
            fallback.localStructure = tuningArgument(prefix + "structure", fallback.localStructure);
            fallback.localTone = tuningArgument(prefix + "tone", fallback.localTone);
            fallback.skinStructure = tuningArgument(prefix + "skin", fallback.skinStructure);
            fallback.autoMask = app.arguments().contains(prefix + "mask") ? 1u : fallback.autoMask;
            return fallback;
        };
        options.nrTuning = tuningFor("--nr-", options.nrTuning);
        const int sourceFps = argument("--source-fps", 0);
        QElapsedTimer measuredCadence;
        options.percent = argument("--percent", 65);
        options.sharpness = argument("--sharpness", 0);
        options.nrPassCount = qBound(1, argument("--passes", 1), 3);
        for (unsigned n = 1; n < options.nrPassCount; ++n) {
            auto &p = options.additionalPasses[n - 1];
            p.tuning = tuningFor(QStringLiteral("--pass%1-").arg(n + 1), options.nrTuning);
            p.percent = argument(QStringLiteral("--pass%1-percent").arg(n + 1), options.percent);
            p.sharpness = argument(QStringLiteral("--pass%1-sharpness").arg(n + 1), options.sharpness);
            p.fsr = app.arguments().contains(QStringLiteral("--pass%1-no-fsr").arg(n + 1)) ? 0u : unsigned(options.fsr);
        }
        int width = argument("--width", 640), height = argument("--height", 360);
        const int soak = argument("--soak-seconds", 0),
                  targetFrames = soak ? std::numeric_limits<int>::max() : argument("--frames", 30);
        const bool expectFailure = app.arguments().contains("--expect-failure");
        const int evidenceArg = app.arguments().indexOf("--evidence-dir");
        const QString evidence = evidenceArg >= 0 && evidenceArg + 1 < app.arguments().size()
                                     ? app.arguments()[evidenceArg + 1]
                                     : QString();
        if (!evidence.isEmpty())
            QDir().mkpath(evidence);
        QElapsedTimer duration;
        duration.start();
        uint64_t changedNrPixels = 0;
        bool verifiedFg = false, fgPresented = false, neuralUnavailable = false;
        std::vector<uint8_t> input(size_t(width) * height * 4), output(input.size());
        bool configured = false, processing = false, nrObserved = false;
        bool applying = false, sawApplying = false;
        QObject::connect(&pipeline, &EnhancementPipeline::applyingChanged, &app,
                         [&](bool value) { applying = value; sawApplying |= value; });
        QObject::connect(&pipeline, &EnhancementPipeline::neuralUnavailable, &app,
                         [&](const QString &) { neuralUnavailable = true; });
        QTimer timeout;
        timeout.setSingleShot(true);
        timeout.start(soak ? soak * 1000 + 60000 : 45000);
        QObject::connect(&timeout, &QTimer::timeout, &app,
                         [&]
                         {
                             qCritical() << "Bridge test timed out";
                             app.quit();
                         });
        QObject::connect(
            &pipeline, &EnhancementPipeline::statusChanged, &app,
            [&](QString message, int, int, int, int, bool nr, bool, bool fg)
            {
                if (completed < 3 || message.contains("unavailable") || message.contains("failed"))
                    qInfo().noquote() << message;
                nrObserved |= nr;
                fgPresented |= fg;
                verifiedFg |= message.contains("FG output verified");
                if (message.contains("crashed") || message.contains("timed out") ||
                    message.startsWith("NVOFA") || message.startsWith("NGX") ||
                    message.contains("could not", Qt::CaseInsensitive) ||
                    message.contains("failed", Qt::CaseInsensitive) ||
                    message.contains("not installed") || message.contains("unavailable") ||
                    message.contains("manifest") || message.contains("package changed"))
                {
                    if (expectFailure)
                        result = 0;
                    app.quit();
                }
                if (message.startsWith("GPU bridge ready"))
                    configured = true;
            });
        QTimer pump;
        pump.setTimerType(Qt::PreciseTimer);
        pump.setInterval(sourceFps ? qMax(1, qRound(1000.0/sourceFps)) : argument("--input-interval-ms", 20));
        QObject::connect(
            &pump, &QTimer::timeout, &app,
            [&]
            {
                if (!configured || processing)
                    return;
                for (int y = 0; y < height; ++y)
                    for (int x = 0; x < width; ++x)
                    {
                        size_t n = (size_t(y) * width + x) * 4;
                        input[n] = uint8_t((x + id * 3) % 256);
                        input[n + 1] = uint8_t((y + id * 7) % 256);
                        input[n + 2] = uint8_t((((x + id * 3) / 11 + (y + id) / 9) % 2) * 180 + 30);
                        input[n + 3] = 255;
                    }
                pl_tex tex = pipeline.beginFrame(id + 1, (id + 1) * (sourceFps ? 1000000/sourceFps : 16667));
                if (!tex)
                    return;
                ++id;
                pl_tex_params textureParams{};
                textureParams.w = width;
                textureParams.h = height;
                textureParams.format = pl_find_named_fmt(vk->gpu, "rgba8");
                textureParams.sampleable = true;
                textureParams.host_writable = true;
                if (!pl_tex_recreate(vk->gpu, &sourceTexture, &textureParams))
                {
                    qCritical() << "Source texture allocation failed";
                    app.quit();
                    return;
                }
                pl_tex_transfer_params transfer{};
                transfer.tex = sourceTexture;
                transfer.ptr = input.data();
                if (!pl_tex_upload(vk->gpu, &transfer))
                {
                    qCritical() << "Input upload failed";
                    app.quit();
                    return;
                }
                pl_frame source{}, nativeTarget{};
                source.num_planes = nativeTarget.num_planes = 1;
                source.planes[0].texture = sourceTexture;
                nativeTarget.planes[0].texture = tex;
                source.planes[0].components = nativeTarget.planes[0].components = 4;
                for (int c = 0; c < 4; ++c)
                    source.planes[0].component_mapping[c] =
                        nativeTarget.planes[0].component_mapping[c] = c;
                source.repr = nativeTarget.repr = pl_color_repr_rgb;
                source.repr.alpha = nativeTarget.repr.alpha = PL_ALPHA_NONE;
                source.color = nativeTarget.color = pl_color_space_srgb;
                source.crop = nativeTarget.crop = {0, 0, float(width), float(height)};
                if (!pl_render_image(renderer, &source, &nativeTarget, &pl_render_default_params))
                {
                    qCritical() << "Native input render failed";
                    app.quit();
                    return;
                }
                processing = pipeline.submitFrame();
                if (processing && app.arguments().contains("--hot-nr-controls") && (id % 5) == 0) {
                    options.nrTuning.intensity = options.nrTuning.intensity == 0.25f ? 0.75f : 0.25f;
                    options.nrTuning.style = (options.nrTuning.style + 1) % 3;
                    options.nrTuning.localTone = options.nrTuning.localTone == 0.25f ? 1.25f : 0.25f;
                    options.nrTuning.autoMask = options.nrTuning.autoMask ? 0u : 1u;
                    for (unsigned pass = 1; !app.arguments().contains("--hot-first-pass-only") && pass < options.nrPassCount; ++pass) {
                        auto &tuning = options.additionalPasses[pass - 1].tuning;
                        tuning.style = (tuning.style + 1) % 3;
                        tuning.localStructure = tuning.localStructure == 0.5f ? 1.5f : 0.5f;
                        tuning.skinStructure = tuning.skinStructure == -1.0f ? 1.5f : -1.0f;
                        tuning.autoMask = 1;
                    }
                    pipeline.prepare(vk, {uint32_t(width), uint32_t(height)}, options, false);
                }
                if (processing && app.arguments().contains("--toggle-fsr-inflight") && (id % 5) == 0) {
                    options.fsr = !options.fsr;
                    options.sharpness = options.fsr ? 20 : 0;
                    for (unsigned pass = 1; pass < options.nrPassCount; ++pass) {
                        auto &settings = options.additionalPasses[pass - 1];
                        settings.fsr = settings.fsr ? 0u : 1u;
                        settings.sharpness = settings.fsr ? 35u : 0u;
                    }
                    pipeline.prepare(vk, {uint32_t(width), uint32_t(height)}, options, false);
                }
            });
        QObject::connect(
            &pipeline, &EnhancementPipeline::frameAvailable, &app,
            [&]
            {
                if (present)
                {
                    pl_frame image{};
                    if (pipeline.outputFrame(image))
                    {
                        QElapsedTimer presentationWork;
                        presentationWork.start();
                        pl_swapchain_frame swap{};
                        if (!pl_swapchain_start_frame(swapchain, &swap))
                        {
                            qCritical() << "Swapchain acquire failed";
                            app.quit();
                            return;
                        }
                        pl_frame target{};
                        pl_frame_from_swapchain(&target, &swap);
                        bool okay =
                            pl_render_image(renderer, &image, &target, &pl_render_default_params);
                        if (!pl_swapchain_submit_frame(swapchain) || !okay)
                        {
                            qCritical() << "Presentation failed";
                            app.quit();
                            return;
                        }
                        if (sourceFps) {
                            const double interval = chiaki_enhancement::presentationInterval(
                                1.0/sourceFps, 1.0/120, false, pipeline.ownsPresentation(), false);
                            if (interval > 0) QThread::usleep(static_cast<unsigned long>(interval*1000000));
                        }
                        pl_swapchain_swap_buffers(swapchain);
                        pipeline.notePresented(uint64_t(presentationWork.nsecsElapsed()/1000));
                        ++presented;
                    }
                }
                if (!present) {
                    pl_frame consumed{};
                    if (pipeline.outputFrame(consumed))
                        pipeline.notePresented(); // Simulate a consumer in compute-only diagnostics.
                }
                if (!processing || pipeline.completedFrameId() != id)
                    return;
                pl_frame frame{};
                if (!pipeline.outputFrame(frame))
                    return;
                // Readback is used only by this diagnostic, never normal streaming.
                pl_tex_transfer_params transfer{};
                transfer.tex = frame.planes[0].texture;
                transfer.ptr = output.data();
                const bool verifyPixels = !sourceFps || completed < 3 || (completed == 9 && !evidence.isEmpty());
                if (verifyPixels && !pl_tex_download(vk->gpu, &transfer))
                {
                    qCritical() << "Output download failed";
                    app.quit();
                    return;
                }
                if (verifyPixels && !options.neural && !options.frameGeneration && input != output)
                {
                    qCritical() << "Round-trip pixels differ on frame" << id;
                    app.quit();
                    return;
                }
                if (options.neural && !nrObserved)
                {
                    qCritical() << "NR requested but not active";
                    app.quit();
                    return;
                }
                processing = false;
                ++completed;
                if (completed == 1) measuredCadence.start();
                if (options.neural && verifyPixels)
                {
                    for (size_t n = 0; n < input.size(); n += 4)
                        if (input[n] != output[n] || input[n + 1] != output[n + 1] ||
                            input[n + 2] != output[n + 2])
                            ++changedNrPixels;
                }
                if (completed == 10 && !evidence.isEmpty())
                {
                    QImage(input.data(), width, height, width * 4, QImage::Format_RGBA8888)
                        .save(QDir(evidence).filePath("input.png"));
                    QImage(output.data(), width, height, width * 4, QImage::Format_RGBA8888)
                        .save(QDir(evidence).filePath("output.png"));
                    const QImage screenshot = captureVideoFrame(vk->gpu, renderer, &frame, nullptr,
                        frame, &pl_render_default_params);
                    if (screenshot.isNull() || !screenshot.save(QDir(evidence).filePath("screenshot.png"))) {
                        qCritical() << "Processed screenshot capture failed"; app.quit(); return;
                    }
                    for (int row=0; row<height; ++row)
                        if (std::memcmp(screenshot.constScanLine(row), output.data()+size_t(row)*width*4, size_t(width)*4) != 0) {
                            qCritical() << "Screenshot pixels differ from processed output"; app.quit(); return;
                        }
                }
                if (completed == 15 &&
                    (app.arguments().contains("--resize") || app.arguments().contains("--reset")))
                {
                    configured = false;
                    if (app.arguments().contains("--resize"))
                    {
                        width += 128;
                        height += 72;
                        input.resize(size_t(width) * height * 4);
                        output.resize(input.size());
                    }
                    pipeline.reset();
                    pipeline.prepare(vk, {uint32_t(width), uint32_t(height)}, options, false);
                }
                if (completed >= targetFrames || (soak && duration.elapsed() >= soak * 1000))
                {
                    if (expectFailure)
                    {
                        qCritical() << "Expected failure did not occur";
                        app.quit();
                        return;
                    }
                    bool nonzeroIntensity = options.nrTuning.intensity > 0;
                    for (unsigned n=1;n<options.nrPassCount;++n) nonzeroIntensity |= options.additionalPasses[n-1].tuning.intensity > 0;
                    if (options.neural && nonzeroIntensity && !changedNrPixels)
                    {
                        qCritical() << "NR output never changed pixels";
                        app.quit();
                        return;
                    }
                    if (options.frameGeneration && !verifiedFg)
                    {
                        qCritical() << "FG output was never verified";
                        app.quit();
                        return;
                    }
                    if (present && options.frameGeneration && !fgPresented)
                    {
                        qCritical() << "No generated frame reached presentation";
                        app.quit();
                        return;
                    }
                    if (present && options.frameGeneration &&
                        (pipeline.realPresentedCount() + 2 < uint64_t(completed) ||
                         pipeline.generatedPresentedCount() + 2 < uint64_t(completed))) {
                        qCritical() << "FG did not present both real and generated phases";
                        app.quit(); return;
                    }
                    if (sourceFps && completed > 2) {
                        const double realFps = (completed-1)*1000.0/measuredCadence.elapsed();
                        qInfo() << "Paced source FPS=" << sourceFps << "completed real FPS=" << realFps;
                        if (realFps < sourceFps*0.80) {
                            qCritical() << "Frame generation reduced real cadence below 80% of paced input";
                            app.quit(); return;
                        }
                    }
                    qInfo() << "Presented phases real=" << pipeline.realPresentedCount()
                            << "generated=" << pipeline.generatedPresentedCount();
                    qInfo() << "Presented frames=" << presented;
                    qInfo() << "PASS:" << completed << "cross-process GPU frames,"
                            << duration.elapsed() << "ms, changed NR pixels=" << changedNrPixels
                            << "FG verified=" << verifiedFg;
                    result = 0;
                    app.quit();
                }
            });
        pipeline.prepare(vk, {uint32_t(width), uint32_t(height)}, options, false);
        pump.start();
        app.exec();
        if (!sawApplying || applying) {
            qCritical() << "Settings apply state did not clear on completion/failure";
            result = 1;
        }
        if (expectFailure && neuralUnavailable) {
            qCritical() << "A pipeline failure was incorrectly classified as unsupported GPU";
            result = 1;
        }
        qInfo() << "Stopping pipeline";
        pipeline.reset();
        qInfo() << "Pipeline stopped";
    }
    if (sourceTexture)
        pl_tex_destroy(vk->gpu, &sourceTexture);
    qInfo() << "Destroying presentation renderer";
    if (renderer)
        pl_renderer_destroy(&renderer);
    qInfo() << "Destroying swapchain";
    if (swapchain)
        pl_swapchain_destroy(&swapchain);
#ifdef Q_OS_WIN
    if (surface)
    {
        auto destroy = reinterpret_cast<PFN_vkDestroySurfaceKHR>(
            instance->get_proc_addr(instance->instance, "vkDestroySurfaceKHR"));
        destroy(instance->instance, surface, nullptr);
    }
#endif
    qInfo() << "Destroying Vulkan device";
    pl_vulkan_destroy(&vk);
    if (instance)
        pl_vk_inst_destroy(&instance);
    pl_log_destroy(&log);
    return result;
}
