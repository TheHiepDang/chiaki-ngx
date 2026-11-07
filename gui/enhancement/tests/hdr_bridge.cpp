// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "../pipeline.h"
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QDebug>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <qfloat16.h>
#include <vector>
extern "C" {
#include <libplacebo/log.h>
}

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "[RTX][HDR][Test] FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

// Actual Vulkan -> D3D12 worker -> Vulkan frames, including luminance > SDR white.
// Without --nr this needs no NVIDIA model runtime; --nr tests the installed model.
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    std::fprintf(stderr, "[RTX][HDR][Test] Creating Vulkan device\n");
    const auto args = app.arguments();
    auto argument = [&](const QString &name, int fallback) {
        const int index = args.indexOf(name);
        return index >= 0 && index + 1 < args.size() ? args[index + 1].toInt() : fallback;
    };
    pl_log_params logging{};
    logging.log_cb = pl_log_simple;
    logging.log_level = PL_LOG_WARN;
    auto log = pl_log_create(PL_API_VER, &logging);
    const char *extensions[] = {"VK_KHR_external_memory_win32", "VK_KHR_external_semaphore_win32"};
    pl_vulkan_params deviceParams = pl_vulkan_default_params;
    deviceParams.opt_extensions = extensions;
    deviceParams.num_opt_extensions = 2;
    auto vk = pl_vulkan_create(log, &deviceParams);
    CHECK(vk);
    std::fprintf(stderr, "[RTX][HDR][Test] Creating renderer and source texture\n");
    auto renderer = pl_renderer_create(log, vk->gpu);
    CHECK(renderer);
    constexpr unsigned width = 384, height = 216;
    std::vector<float> pixels(width * height * 4), expected(pixels.size()), output(pixels.size());
    pl_tex_params textureParams{};
    textureParams.w = width;
    textureParams.h = height;
    textureParams.format = pl_find_named_fmt(vk->gpu, "rgba16f");
    textureParams.sampleable = textureParams.host_writable = true;
    CHECK(textureParams.format);
    std::fprintf(stderr, "[RTX][HDR][Test] format=%s bytes=%zu host_bits=%d\n", textureParams.format->name, textureParams.format->texel_size, textureParams.format->host_bits[0]);
    pl_tex sourceTexture = pl_tex_create(vk->gpu, &textureParams);
    CHECK(sourceTexture);
    {
        EnhancementPipeline pipeline;
        std::fprintf(stderr, "[RTX][HDR][Test] Pipeline initialized\n");
        EnhancementOptions options;
        options.bridgeTest = true;
        options.neural = args.contains("--nr");
        options.frameGeneration = args.contains("--fg");
        options.percent = argument("--percent", 65);
        options.nrPassCount = unsigned(argument("--passes", 1));
        options.sharpness = argument("--sharpness", 0);
        for (auto &pass : options.additionalPasses) {
            pass.percent = unsigned(options.percent);
            pass.sharpness = unsigned(options.sharpness);
        }
        bool failed = false, nrActive = false, fgActive = false;
        QString failure;
        QObject::connect(&pipeline, &EnhancementPipeline::statusChanged, &app,
            [&](const QString &message, int, int, int, int, bool nr, bool, bool fg) {
                nrActive = nr;
                fgActive = fg;
                if (message.contains("failed", Qt::CaseInsensitive) ||
                    message.contains("timed out") || message.contains("NR unavailable") ||
                    message.contains("could not", Qt::CaseInsensitive) || message.contains("not installed") ||
                    message.contains("manifest") || message.contains("crashed")) {
                    failed = true;
                    failure = message;
                }
            });
        auto waitFor = [&](auto predicate) {
            QElapsedTimer elapsed;
            elapsed.start();
            while (!predicate() && !failed && elapsed.elapsed() < 45000) {
                app.processEvents();
                QThread::msleep(2);
            }
            if (failed) qCritical().noquote() << failure;
            return !failed && predicate();
        };
        const bool transitions = !options.neural || args.contains("--transitions");
        const unsigned phaseCount = transitions ? 4 : 1;
        uint64_t frameId = 0;
        bool anyEdit = false;
        for (unsigned phase = 0; phase < phaseCount; ++phase) {
            std::fprintf(stderr, "[RTX][HDR][Test] Phase %u\n", phase);
            // NR-only smoke tests start in HDR. Bridge tests also cross SDR/PQ/HLG boundaries.
            const bool hdr = !transitions || phase == 1 || phase == 2;
            pl_color_space color = hdr ? pl_color_space_hdr10 : pl_color_space_srgb;
            if (hdr) {
                color.hdr.max_luma = 4000;
                color.hdr.min_luma = PL_COLOR_HDR_BLACK;
                if (phase == 2) color.transfer = PL_COLOR_TRC_HLG;
            }
            pipeline.prepare(vk, {width, height}, options, color);
            const auto working = pipeline.inputColor();
            CHECK(pl_color_space_is_hdr(&working) == hdr);
            CHECK(!hdr || (working.transfer == PL_COLOR_TRC_LINEAR && working.primaries == PL_COLOR_PRIM_BT_2020));
            for (int frame = 0; frame < 4; ++frame) {
                // Quantization and the shared resource format differ in SDR; only HDR phase
                // readbacks use this FP16 buffer. SDR transitions still submit actual frames.
                for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x) {
                    const size_t n = (size_t(y) * width + x) * 4;
                    const float level = x < width/2 ? float((x + frame * 3) % (width/2)) / (width/2) :
                        (hdr ? 20.0f : 1.0f);
                    pixels[n] = level;
                    pixels[n+1] = level * (y % 17 == 0 ? 0.1f : 1.0f);
                    pixels[n+2] = level * (y % 19 == 0 ? 0.05f : 1.0f);
                    pixels[n+3] = 1.0f;
                }
                if (hdr) {
                    for (size_t n = 0; n < pixels.size(); n += 4)
                        pl_color_delinearize(&color, pixels.data() + n);
                }
                pl_tex_transfer_params upload{};
                upload.tex = sourceTexture;
                upload.ptr = pixels.data();
                CHECK(pl_tex_upload(vk->gpu, &upload));
                pl_tex shared = nullptr;
                ++frameId;
                CHECK(waitFor([&] { shared = pipeline.beginFrame(frameId, frameId * 16667); return shared != nullptr; }));
                pl_frame source{}, target{};
                source.num_planes = target.num_planes = 1;
                source.planes[0].texture = sourceTexture;
                target.planes[0].texture = shared;
                source.planes[0].components = target.planes[0].components = 4;
                for (int k = 0; k < 4; ++k) source.planes[0].component_mapping[k] = target.planes[0].component_mapping[k] = k;
                source.repr = target.repr = pl_color_repr_rgb;
                source.repr.alpha = target.repr.alpha = PL_ALPHA_NONE;
                source.color = color;
                target.color = working;
                source.crop = target.crop = {0, 0, float(width), float(height)};
                CHECK(pl_render_image(renderer, &source, &target, &pl_render_default_params));
                auto readPixels = [&](pl_tex texture, std::vector<float> &values) {
                    // Wrapped native FP16 can expose 16-bit host data, while libplacebo's
                    // emulated "rgba16f" upload format exposes 32-bit host floats.
                    const auto format = texture->params.format;
                    std::vector<uint8_t> raw(size_t(width) * height * format->texel_size);
                    pl_tex_transfer_params download{};
                    download.tex = texture;
                    download.ptr = raw.data();
                    if (!pl_tex_download(vk->gpu, &download)) return false;
                    if (format->host_bits[0] == 16) {
                        for (size_t n = 0; n < values.size(); ++n) {
                            qfloat16 half;
                            std::memcpy(&half, raw.data() + n * 2, 2);
                            values[n] = float(half);
                        }
                    } else {
                        if (format->host_bits[0] != 32) return false;
                        std::memcpy(values.data(), raw.data(), values.size() * sizeof(float));
                    }
                    return true;
                };
                if (hdr) CHECK(readPixels(shared, expected));
                if (hdr && frame == 0) {
                    const size_t middle = (size_t(height / 2) * width + width / 4) * 4;
                    CHECK(std::abs(expected[middle] - 0.5f) < 0.02f);
                }
                CHECK(pipeline.submitFrame());
                if (hdr && frame == 1) {
                    auto newMetadata = color;
                    newMetadata.hdr.max_luma = 2000;
                    pipeline.prepare(vk, {width, height}, options, newMetadata);
                    CHECK(pipeline.inputColor().hdr.max_luma == 2000);
                }
                CHECK(waitFor([&] { return pipeline.completedFrameId() == frameId; }));
                pl_frame result{};
                CHECK(pipeline.outputFrame(result));
                CHECK(result.color.transfer == working.transfer);
                if (hdr && frame == 1) pipeline.prepare(vk, {width, height}, options, color);
                CHECK(result.color.primaries == working.primaries);
                CHECK(!hdr || result.color.hdr.max_luma == 4000);
                CHECK(!options.neural || nrActive);
                CHECK(!hdr || !fgActive);
                if (hdr) {
                    CHECK(readPixels(result.planes[0].texture, output));
                    float brightest = 0;
                    for (size_t n = 0; n < output.size(); ++n) {
                        const float value = float(output[n]);
                        CHECK(std::isfinite(value));
                        brightest = std::max(brightest, value);
                        if (!options.neural) CHECK(output[n] == expected[n]);
                        anyEdit |= std::abs(value - float(expected[n])) > 0.001f;
                    }
                    CHECK(brightest > 10.0f && brightest < 100.0f);
                }
                pipeline.notePresented();
                // Held frames retain their own color metadata without a new source frame.
                CHECK(pipeline.outputFrame(result));
                CHECK(result.color.transfer == working.transfer);
            }
        }
        CHECK(!options.neural || anyEdit);
        qInfo() << "[RTX][HDR][Test] PASS: shared HDR highlights, color metadata, held frames, transitions="
                << transitions << "NR=" << options.neural << "passes=" << options.nrPassCount;
    }
    pl_tex_destroy(vk->gpu, &sourceTexture);
    pl_renderer_destroy(&renderer);
    pl_vulkan_destroy(&vk);
    pl_log_destroy(&log);
    return 0;
}
