static void blit_resampled_rgba(const std::vector<uint8_t>& src,
                                int src_w,
                                int src_h,
                                std::vector<uint8_t>& dst,
                                int dst_w,
                                int dst_h,
                                int dst_x,
                                int dst_y,
                                int copy_w,
                                int copy_h)
{
    if (src.empty() || src_w <= 0 || src_h <= 0 ||
        dst.empty() || dst_w <= 0 || dst_h <= 0 ||
        copy_w <= 0 || copy_h <= 0) return;

    const int clipped_w = std::min(copy_w, dst_w - dst_x);
    const int clipped_h = std::min(copy_h, dst_h - dst_y);
    if (dst_x < 0 || dst_y < 0 || clipped_w <= 0 || clipped_h <= 0) return;

    for (int y = 0; y < clipped_h; ++y) {
        const float sy = (float(y) + 0.5f) * float(src_h) / float(copy_h) - 0.5f;
        const int y0 = std::clamp(int(std::floor(sy)), 0, src_h - 1);
        const int y1 = std::min(y0 + 1, src_h - 1);
        const float fy = std::clamp(sy - float(y0), 0.0f, 1.0f);
        for (int x = 0; x < clipped_w; ++x) {
            const float sx = (float(x) + 0.5f) * float(src_w) / float(copy_w) - 0.5f;
            const int x0 = std::clamp(int(std::floor(sx)), 0, src_w - 1);
            const int x1 = std::min(x0 + 1, src_w - 1);
            const float fx = std::clamp(sx - float(x0), 0.0f, 1.0f);

            const uint8_t* p00 = src.data() + (size_t(y0) * src_w + x0) * 4;
            const uint8_t* p10 = src.data() + (size_t(y0) * src_w + x1) * 4;
            const uint8_t* p01 = src.data() + (size_t(y1) * src_w + x0) * 4;
            const uint8_t* p11 = src.data() + (size_t(y1) * src_w + x1) * 4;
            uint8_t* out = dst.data() + (size_t(dst_y + y) * dst_w + (dst_x + x)) * 4;
            const float w00 = (1.0f - fx) * (1.0f - fy);
            const float w10 = fx * (1.0f - fy);
            const float w01 = (1.0f - fx) * fy;
            const float w11 = fx * fy;
            for (int c = 0; c < 4; ++c) {
                out[c] = uint8_t(std::clamp(
                    w00 * p00[c] + w10 * p10[c] +
                    w01 * p01[c] + w11 * p11[c],
                    0.0f, 255.0f));
            }
        }
    }
}

static bool DecodeEhfEmbeddedTileComposite(const std::vector<uint8_t>& ehf,
                                           uint32_t terrain_vertices_w,
                                           uint32_t terrain_vertices_h,
                                           std::vector<uint8_t>& out_rgba,
                                           int& out_w,
                                           int& out_h)
{
    out_rgba.clear();
    out_w = 0;
    out_h = 0;
    const uint32_t cells_w =
        terrain_vertices_w > 1 ? terrain_vertices_w - 1 : terrain_vertices_w;
    const uint32_t cells_h =
        terrain_vertices_h > 1 ? terrain_vertices_h - 1 : terrain_vertices_h;
    if (cells_w == 0 || cells_h == 0) return false;

    std::vector<EhfRenderTileDesc> tiles;
    if (!parse_ehf_render_tiles(ehf, cells_w, tiles) || tiles.empty()) {
        return false;
    }
    std::vector<EhfEmbeddedBc1Mip> primaries =
        collect_ehf_embedded_bc1_primaries(ehf);
    if (primaries.size() < tiles.size()) return false;

    struct DecodedTile {
        bool ok = false;
        std::vector<uint8_t> rgba;
        int w = 0;
        int h = 0;
    };
    std::vector<DecodedTile> decoded(tiles.size());
    std::vector<float> ratios_x;
    std::vector<float> ratios_y;
    size_t ok_count = 0;
    for (size_t i = 0; i < tiles.size(); ++i) {
        DecodedTile dt;
        if (!decode_ehf_embedded_bc1(ehf, primaries[i], dt.rgba, dt.w, dt.h)) {
            decoded[i] = std::move(dt);
            continue;
        }
        dt.ok = true;
        if (tiles[i].cell_w > 0 && tiles[i].cell_h > 0) {
            ratios_x.push_back(float(dt.w) / float(tiles[i].cell_w));
            ratios_y.push_back(float(dt.h) / float(tiles[i].cell_h));
        }
        decoded[i] = std::move(dt);
        ++ok_count;
    }
    if (ok_count < std::max<size_t>(4, tiles.size() / 4)) return false;

    auto median_ratio = [](std::vector<float>& v) -> float {
        if (v.empty()) return 1.0f;
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    };
    const float median_x = median_ratio(ratios_x);
    const float median_y = median_ratio(ratios_y);
    if (median_x < 0.5f || median_y < 0.5f ||
        median_x > 8.0f || median_y > 8.0f)
    {
        std::ostringstream os;
        os << "ehf: embedded BC1 tile pages look strip-like "
           << "(median scale=" << median_x << "x" << median_y
           << "), skipping as terrain albedo";
        OutputLog::info(os.str());
        return false;
    }

    const int scale_x = std::clamp(int(std::lround(median_x)), 1, 8);
    const int scale_y = std::clamp(int(std::lround(median_y)), 1, 8);

    out_w = int(cells_w) * scale_x;
    out_h = int(cells_h) * scale_y;
    if (out_w <= 0 || out_h <= 0 || out_w > 8192 || out_h > 8192) {
        return false;
    }
    out_rgba.assign(size_t(out_w) * size_t(out_h) * 4, 0);
    for (size_t i = 3; i < out_rgba.size(); i += 4) {
        out_rgba[i] = 0xFF;
    }

    for (size_t i = 0; i < tiles.size(); ++i) {
        const DecodedTile& dt = decoded[i];
        if (!dt.ok) continue;
        const EhfRenderTileDesc& t = tiles[i];
        const int dx = int(t.cell_x) * scale_x;
        const int dy = int(t.cell_y) * scale_y;
        const int dw = int(t.cell_w) * scale_x;
        const int dh = int(t.cell_h) * scale_y;
        blit_resampled_rgba(dt.rgba, dt.w, dt.h,
                            out_rgba, out_w, out_h,
                            dx, dy, dw, dh);
    }

    std::ostringstream os;
    os << "ehf: embedded tile composite " << out_w << "x" << out_h
       << " from " << ok_count << "/" << tiles.size()
       << " tile texture pages (scale=" << scale_x << "x" << scale_y << ")";
    OutputLog::success(os.str());
    return true;
}
