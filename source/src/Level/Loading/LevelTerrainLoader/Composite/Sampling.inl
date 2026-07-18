    auto sample_mat = [&](int idx, float u_world, float v_world,
                          uint8_t out_rgb[3])
    {
        const Mat& m = (idx >= 0 && idx < (int)mats.size() && mats[idx].decoded)
            ? mats[idx] : mats[first_decoded];
        const float ts = (m.tile_scale > 0.f && m.tile_scale < 1.f)
                            ? m.tile_scale : 0.125f;
        float u = (u_world * ts);
        float v = (v_world * ts);
        u = u - std::floor(u);
        v = v - std::floor(v);
        const float fx = u * m.w;
        const float fy = v * m.h;
        const int x0 = int(fx);
        const int y0 = int(fy);
        const int x1 = (x0 + 1) % m.w;
        const int y1 = (y0 + 1) % m.h;
        const float dx = fx - float(x0);
        const float dy = fy - float(y0);
        const uint8_t* p00 = m.rgba.data() + (size_t(y0) * m.w + x0) * 4;
        const uint8_t* p10 = m.rgba.data() + (size_t(y0) * m.w + x1) * 4;
        const uint8_t* p01 = m.rgba.data() + (size_t(y1) * m.w + x0) * 4;
        const uint8_t* p11 = m.rgba.data() + (size_t(y1) * m.w + x1) * 4;
        const float w00b = (1.f - dx) * (1.f - dy);
        const float w10b =        dx  * (1.f - dy);
        const float w01b = (1.f - dx) *        dy;
        const float w11b =        dx  *        dy;
        for (int c = 0; c < 3; ++c) {
            out_rgb[c] = uint8_t(
                w00b * p00[c] + w10b * p10[c] +
                w01b * p01[c] + w11b * p11[c]);
        }
    };

    auto sample_mask = [&](const EhfChunkLayer& L,
                           float local_x, float local_z) -> float
    {
        if (parsed.splat_indices.empty() ||
            parsed.splat_w == 0 || parsed.splat_h == 0 ||
            parsed.splat_indices.size() !=
                size_t(parsed.splat_w) * size_t(parsed.splat_h))
        {
            return 1.0f;
        }

        const float scale_u = (L.mask_scale[0] > 0.0f)
            ? L.mask_scale[0]
            : 32.0f / float(parsed.splat_w);
        const float scale_v = (L.mask_scale[1] > 0.0f)
            ? L.mask_scale[1]
            : 32.0f / float(parsed.splat_h);

        const float u = L.tile_uv[0]
            + std::clamp(local_x, 0.0f, 1.0f)
            * scale_u * 2.0f;
        const float v = L.tile_uv[1]
            + std::clamp(local_z, 0.0f, 1.0f)
            * scale_v * 2.0f;

        float px = u * float(parsed.splat_w) - 0.5f;
        float py = v * float(parsed.splat_h) - 0.5f;
        px = std::clamp(px, 0.0f, float(parsed.splat_w - 1));
        py = std::clamp(py, 0.0f, float(parsed.splat_h - 1));

        const int x0 = int(px);
        const int y0 = int(py);
        const int x1 = std::min<int>(x0 + 1, int(parsed.splat_w) - 1);
        const int y1 = std::min<int>(y0 + 1, int(parsed.splat_h) - 1);
        const float dx = px - float(x0);
        const float dy = py - float(y0);
        auto at = [&](int x, int y) -> float {
            return parsed.splat_indices[
                size_t(y) * size_t(parsed.splat_w) + size_t(x)] / 255.0f;
        };
        const float w00m = (1.0f - dx) * (1.0f - dy);
        const float w10m =         dx  * (1.0f - dy);
        const float w01m = (1.0f - dx) *         dy;
        const float w11m =         dx  *         dy;
        return std::clamp(at(x0, y0) * w00m + at(x1, y0) * w10m
                        + at(x0, y1) * w01m + at(x1, y1) * w11m,
                          0.0f, 1.0f);
    };

    constexpr float kBlendMax     = 3.0f;
