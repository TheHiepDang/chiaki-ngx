// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#pragma once
extern "C" {
#include <libplacebo/shaders/custom.h>
#include <libplacebo/shaders/colorspace.h>
}

// Render-thread-local, final-video adjustment. Uniform values are copied by
// libplacebo during rendering; changing a slider does not recompile the shader.
struct EyeCandyTone {
    float exposureEv = 0.0f;
    float darkAreaEv = 0.0f;
    pl_hook hook = {};

    EyeCandyTone(float exposure, float darkArea) : exposureEv(exposure), darkAreaEv(darkArea) {
        hook.stages = PL_HOOK_OUTPUT;
        hook.input = PL_HOOK_SIG_COLOR;
        hook.priv = this;
        hook.signature = 0x434849414b494543ULL;
        hook.hook = [](void *priv, const pl_hook_params *p) {
            const auto &tone = *static_cast<const EyeCandyTone *>(priv);
            pl_hook_res result = {};
            // Modify the supplied shader in place, preserving its size and
            // representation. No replacement texture/shader is returned.
            result.output = PL_HOOK_SIG_NONE;
            if (pl_color_space_is_hdr(&p->color)) return result;

            const auto primaries = pl_raw_primaries_get(p->color.primaries);
            const auto rgbToXyz = pl_get_rgb2xyz_matrix(primaries ? primaries : pl_raw_primaries_get(PL_COLOR_PRIM_BT_709));
            const float lumaWeights[] = {rgbToXyz.m[1][0], rgbToXyz.m[1][1], rgbToXyz.m[1][2]};
            pl_shader_var vars[3] = {};
            vars[0].var = pl_var_float("eye_exposure");
            vars[0].data = &tone.exposureEv;
            vars[0].dynamic = true;
            vars[1].var = pl_var_float("eye_dark_area");
            vars[1].data = &tone.darkAreaEv;
            vars[1].dynamic = true;
            vars[2].var = pl_var_vec3("eye_luma_weights");
            vars[2].data = lumaWeights;
            pl_custom_shader custom = {};
            custom.description = "Eye Candy exposure and dark area";
            custom.input = custom.output = PL_SHADER_SIG_COLOR;
            custom.variables = vars;
            custom.num_variables = 3;
            // Use the pre-exposure luminance for the shadow mask, preserving
            // black and smoothly fading the effect out by 18% linear luminance.
            custom.body = R"(
                float eye_luma = max(dot(color.rgb, eye_luma_weights), 0.0);
                float eye_shadow_weight = 1.0 - smoothstep(0.0, 0.18, eye_luma);
                color.rgb *= exp2(eye_exposure + eye_dark_area * eye_shadow_weight);
                color.rgb = clamp(color.rgb, 0.0, 1.0);
            )";
            auto workingColor = p->color;
            workingColor.hdr.min_luma = 1e-7f; // Grade relative to image black, not display black.
            pl_shader_linearize(p->sh, &workingColor);
            result.failed = !pl_shader_custom(p->sh, &custom);
            pl_shader_delinearize(p->sh, &workingColor);
            return result;
        };
    }
    bool active() const { return exposureEv != 0.0f || darkAreaEv != 0.0f; }
};
