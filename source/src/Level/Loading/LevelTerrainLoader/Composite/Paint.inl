    for (int y = 0; y < lm_h; ++y) {
        const float v_norm = (lm_h > 1)
            ? float(y) / float(lm_h - 1)
            : 0.0f;
        const float world_z = world_min_z + v_norm * world_span_z;
        const float fy_chunk = (world_z - world_min_z) / chunk_size_z;
        const int   cy       = std::min<int>(parsed.chunk_h - 1, int(fy_chunk));
        const float fy_in    = std::clamp(fy_chunk - float(cy), 0.f, 1.f);
        for (int x = 0; x < lm_w; ++x) {
            const float u_norm = (lm_w > 1)
                ? float(x) / float(lm_w - 1)
                : 0.0f;
            const float world_x = world_min_x + u_norm * world_span_x;
            const float fx_chunk = (world_x - world_min_x) / chunk_size_x;
            const int   cx       = std::min<int>(parsed.chunk_w - 1, int(fx_chunk));
            const float fx_in    = std::clamp(fx_chunk - float(cx), 0.f, 1.f);

            const float w00 = (1.f - fx_in) * (1.f - fy_in);
            const float w10 =        fx_in  * (1.f - fy_in);
            const float w01 = (1.f - fx_in) *        fy_in;
            const float w11 =        fx_in  *        fy_in;

            const size_t chunk_index =
                size_t(cy) * size_t(parsed.chunk_w) + size_t(cx);
            const EhfChunk& chunk =
                (chunk_index < chunk_grid.size() && chunk_grid[chunk_index])
                    ? *chunk_grid[chunk_index]
                    : parsed.chunks.front();

            float accum_r = 0.f, accum_g = 0.f, accum_b = 0.f;
            float accum_a = 0.f;
            uint8_t first_rgb[3] = {0, 0, 0};
            bool have_first_rgb = false;

            const float wu = world_x;
            const float wv = world_z;

            for (const auto& L : chunk.layers) {
                auto corner_material = [&](int corner) -> int {
                    const uint32_t layer_idx = L.texture_idx[corner];
                    uint32_t mat_idx = L.material_idx;
                    if (layer_idx < chunk.layers.size()) {
                        mat_idx = chunk.layers[size_t(layer_idx)].material_idx;
                    }
                    if (mat_idx < mats.size()) return int(mat_idx);
                    if (L.material_idx < mats.size()) return int(L.material_idx);
                    return -1;
                };
                const int material_ids[4] = {
                    corner_material(0), corner_material(1),
                    corner_material(2), corner_material(3),
                };
                const float corner_alpha[4] = {
                    w00 * float(L.blend[0]) / kBlendMax,
                    w10 * float(L.blend[1]) / kBlendMax,
                    w01 * float(L.blend[2]) / kBlendMax,
                    w11 * float(L.blend[3]) / kBlendMax,
                };
                const float blend_px = corner_alpha[0] + corner_alpha[1] +
                                       corner_alpha[2] + corner_alpha[3];
                if (blend_px <= 1e-6f) continue;

                float rgb_f[3] = {0.0f, 0.0f, 0.0f};
                bool have_corner_rgb = false;
                for (int ci = 0; ci < 4; ++ci) {
                    if (material_ids[ci] < 0 || corner_alpha[ci] <= 0.0f) {
                        continue;
                    }
                    uint8_t corner_rgb[3];
                    sample_mat(material_ids[ci], wu, wv, corner_rgb);
                    const float w = corner_alpha[ci] / blend_px;
                    rgb_f[0] += float(corner_rgb[0]) * w;
                    rgb_f[1] += float(corner_rgb[1]) * w;
                    rgb_f[2] += float(corner_rgb[2]) * w;
                    have_corner_rgb = true;
                }
                if (!have_corner_rgb) continue;
                uint8_t rgb[3];
                rgb[0] = uint8_t(std::clamp(int(std::round(rgb_f[0])), 0, 255));
                rgb[1] = uint8_t(std::clamp(int(std::round(rgb_f[1])), 0, 255));
                rgb[2] = uint8_t(std::clamp(int(std::round(rgb_f[2])), 0, 255));
                if (!have_first_rgb) {
                    first_rgb[0] = rgb[0];
                    first_rgb[1] = rgb[1];
                    first_rgb[2] = rgb[2];
                    have_first_rgb = true;
                }

                const float alpha = std::clamp(blend_px, 0.f, 1.f)
                                  * sample_mask(L, fx_in, fy_in);
                if (alpha < 1.f / 255.f) continue;

                const float keep = 1.0f - alpha;
                accum_r = accum_r * keep + float(rgb[0]) * alpha;
                accum_g = accum_g * keep + float(rgb[1]) * alpha;
                accum_b = accum_b * keep + float(rgb[2]) * alpha;
                accum_a = accum_a * keep + alpha;
            }

            if (accum_a < 0.999f && have_first_rgb) {
                const float fill = 1.0f - accum_a;
                accum_r += float(first_rgb[0]) * fill;
                accum_g += float(first_rgb[1]) * fill;
                accum_b += float(first_rgb[2]) * fill;
                accum_a = 1.0f;
            }

            if (accum_a > 1e-4f && accum_a < 0.999f) {
                accum_r /= accum_a;
                accum_g /= accum_a;
                accum_b /= accum_a;
            } else {
                uint8_t base[3];
                sample_mat(first_decoded, wu, wv, base);
                if (!have_first_rgb) {
                    accum_r = base[0]; accum_g = base[1]; accum_b = base[2];
                }
            }

            uint8_t* dst = out_rgba.data() + (size_t(y) * lm_w + x) * 4;
            dst[0] = uint8_t(std::clamp(accum_r, 0.f, 255.f));
            dst[1] = uint8_t(std::clamp(accum_g, 0.f, 255.f));
            dst[2] = uint8_t(std::clamp(accum_b, 0.f, 255.f));
            dst[3] = 0xFF;
        }
    }

    std::ostringstream os;
    os << "bake composite: " << lm_w << "x" << lm_h
       << " (chunk grid " << parsed.chunk_w << "x" << parsed.chunk_h
       << " x " << parsed.lods.size() << " LODs x multi-layer)";
    OutputLog::success(os.str());
    return true;
}
