// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "../eye_candy.h"
#include <QGuiApplication>
#include <QDebug>
#include <array>
#include <cmath>
extern "C" {
#include <libplacebo/vulkan.h>
#include <libplacebo/renderer.h>
#include <libplacebo/log.h>
}

#define CHECK(x) do { if (!(x)) { qCritical() << "FAIL" << __LINE__ << #x; return 1; } } while (0)
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    pl_log_params logParams = {};
    logParams.log_cb = pl_log_simple;
    logParams.log_level = PL_LOG_WARN;
    auto log = pl_log_create(PL_API_VER, &logParams);
    auto vk = pl_vulkan_create(log, nullptr);
    CHECK(vk);
    auto renderer = pl_renderer_create(log, vk->gpu);
    CHECK(renderer);
    constexpr int width = 8, height = 2;
    const float ramp[] = {0, 0.05f, 0.10f, 0.25f, 0.5f, 0.65f, 0.8f, 1};
    std::array<float, width * height * 4> pixels{}, baseline{}, neutral{}, output{};
    for (int n = 0; n < width * height; ++n) {
        pixels[n*4] = pixels[n*4+1] = pixels[n*4+2] = ramp[n % width];
        pixels[n*4+3] = 0.75f;
    }
    pl_tex_params textureParams = {};
    textureParams.w = width;
    textureParams.h = height;
    textureParams.format = pl_find_named_fmt(vk->gpu, "rgba32f");
    textureParams.sampleable = textureParams.renderable = true;
    textureParams.host_writable = textureParams.host_readable = true;
    CHECK(textureParams.format);
    auto sourceTexture = pl_tex_create(vk->gpu, &textureParams);
    auto targetTexture = pl_tex_create(vk->gpu, &textureParams);
    CHECK(sourceTexture && targetTexture);
    pl_tex_transfer_params transfer = {};
    transfer.tex = sourceTexture;
    transfer.ptr = pixels.data();
    CHECK(pl_tex_upload(vk->gpu, &transfer));
    pl_frame source = {}, target = {};
    source.num_planes = target.num_planes = 1;
    source.planes[0].texture = sourceTexture;
    target.planes[0].texture = targetTexture;
    source.planes[0].components = target.planes[0].components = 4;
    for (int n = 0; n < 4; ++n) source.planes[0].component_mapping[n] = target.planes[0].component_mapping[n] = n;
    source.repr = target.repr = pl_color_repr_rgb;
    source.repr.alpha = target.repr.alpha = PL_ALPHA_INDEPENDENT;
    source.color = target.color = pl_color_space_srgb;
    source.crop = target.crop = {0, 0, float(width), float(height)};
    bool useMix = false;
    auto render = [&](float exposure, float darkArea, bool enable, auto &out) {
        EyeCandyTone tone(exposure, darkArea);
        const pl_hook *hooks[] = {&tone.hook};
        pl_render_params params = pl_render_default_params;
        params.dither_params = nullptr;
        params.background_transparency = 1.0f;
        if (enable) { params.hooks = hooks; params.num_hooks = 1; }
        const pl_frame *frames[] = {&source};
        const uint64_t signatures[] = {1};
        const float timestamps[] = {0};
        pl_frame_mix mix = {};
        mix.num_frames = 1; mix.frames = frames; mix.signatures = signatures; mix.timestamps = timestamps; mix.vsync_duration = 1.0f / 60.0f;
        if (!(useMix ? pl_render_image_mix(renderer, &mix, &target, &params)
                     : pl_render_image(renderer, &source, &target, &params))) return false;
        const auto state = pl_renderer_get_errors(renderer);
        if (state.errors || state.num_disabled_hooks) return false;
        pl_tex_transfer_params download = {};
        download.tex = targetTexture;
        download.ptr = out.data();
        return pl_tex_download(vk->gpu, &download);
    };
    auto close = [](float a, float b) { return std::abs(a-b) < 0.002f; };
    CHECK(render(0, 0, false, baseline));
    CHECK(render(0, 0, true, neutral));
    for (size_t n = 0; n < pixels.size(); ++n) CHECK(close(neutral[n], baseline[n]));
    CHECK(render(1, 0, true, output));
    for (int n = 0; n < width * height; ++n) {
        const float value = ramp[n % width];
        const float linear = value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
        const float exposed = std::fmin(1.0f, linear * 2.0f);
        const float expected = exposed <= 0.0031308f ? exposed * 12.92f : 1.055f * std::pow(exposed, 1.0f/2.4f) - 0.055f;
        if (!close(output[n*4], expected)) qInfo() << "sample" << n << "input" << value << "baseline" << baseline[n*4] << "output" << output[n*4] << "expected" << expected << "alpha" << output[n*4+3];
        CHECK(close(output[n*4], expected));
        CHECK(close(output[n*4+3], baseline[n*4+3]));
    }
    CHECK(render(-1, 0, true, output));
    CHECK(output[4*4] < baseline[4*4]);
    CHECK(render(0, 1, true, output));
    CHECK(close(output[0], 0));
    CHECK(output[2*4] > baseline[2*4] + 0.02f);
    CHECK(close(output[6*4], baseline[6*4]));
    CHECK(render(0, -1, true, output));
    CHECK(output[2*4] < baseline[2*4] - 0.01f);
    CHECK(close(output[6*4], baseline[6*4]));
    // Repainting the same cached source must pick up new uniform values.
    useMix = true;
    CHECK(render(0, 0, true, neutral));
    CHECK(render(0.5f, 0, true, output));
    CHECK(output[4*4] > neutral[4*4] + 0.05f);
    CHECK(render(0, 1, true, output));
    CHECK(output[2*4] > neutral[2*4] + 0.02f);
    CHECK(close(output[6*4], neutral[6*4]));
    useMix = false;
    source.color.transfer = target.color.transfer = PL_COLOR_TRC_PQ;
    source.color.hdr.max_luma = target.color.hdr.max_luma = 1000;
    CHECK(render(0, 0, false, neutral));
    CHECK(render(1, 1, true, output));
    for (size_t n = 0; n < pixels.size(); ++n) CHECK(close(output[n], neutral[n]));
    // A hook cannot silently fail and leave an unchanged picture.
    const auto errors = pl_renderer_get_errors(renderer);
    CHECK(errors.errors == 0 && errors.num_disabled_hooks == 0);
    pl_tex_destroy(vk->gpu, &sourceTexture);
    pl_tex_destroy(vk->gpu, &targetTexture);
    pl_renderer_destroy(&renderer);
    pl_vulkan_destroy(&vk);
    pl_log_destroy(&log);
    qInfo() << "PASS: actual Vulkan tone output; neutral identity; exposure stops; shadow lift/deepen; cached edits; HDR bypass; highlights, black and alpha preserved";
    return 0;
}
