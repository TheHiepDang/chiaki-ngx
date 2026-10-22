// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#pragma once
#include <QImage>
extern "C" {
#include <libplacebo/renderer.h>
}

// Occasional SDR still readback, after video processing and before UI overlays.
inline QImage captureVideoFrame(pl_gpu gpu, pl_renderer renderer, const pl_frame *source,
                               const pl_frame_mix *mix, const pl_frame &reference,
                               const pl_render_params *params)
{
    if (!reference.num_planes || !reference.planes[0].texture) return {};
    const auto &size = reference.planes[0].texture->params;
    pl_tex_params tp = {};
    tp.w = size.w; tp.h = size.h;
    tp.format = pl_find_named_fmt(gpu, "rgba8");
    tp.renderable = true; tp.host_readable = true;
    if (!tp.format) return {};
    pl_tex tex = pl_tex_create(gpu, &tp);
    if (!tex) return {};
    pl_frame target = {};
    target.num_planes = 1; target.planes[0].texture = tex;
    target.planes[0].components = 4;
    for (int i=0;i<4;++i) target.planes[0].component_mapping[i]=i;
    target.crop = reference.crop;
    target.repr = pl_color_repr_rgb; target.repr.alpha = PL_ALPHA_NONE;
    target.color = pl_color_space_srgb;
    const bool rendered = mix ? pl_render_image_mix(renderer, mix, &target, params)
                              : pl_render_image(renderer, source, &target, params);
    QImage image(size.w, size.h, QImage::Format_RGBA8888);
    pl_tex_transfer_params transfer = {};
    transfer.tex = tex; transfer.ptr = image.bits(); transfer.row_pitch = image.bytesPerLine();
    const bool okay = rendered && !image.isNull() && pl_tex_download(gpu, &transfer);
    pl_tex_destroy(gpu, &tex);
    return okay ? image : QImage();
}
