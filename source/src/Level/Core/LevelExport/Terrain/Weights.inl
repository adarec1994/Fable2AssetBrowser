struct TerrainWeightExport {
    int width = 0;
    int height = 0;
    int material_count = 0;
    std::vector<std::vector<uint8_t>> rgba;
};

bool build_terrain_weight_maps(const EhfParsedBody& parsed,
                               TerrainWeightExport& out)
{
    out = {};
    const bool has_pf99 =
        !parsed.splat_indices.empty() &&
        parsed.splat_w > 0 && parsed.splat_h > 0 &&
        parsed.splat_indices.size() ==
            size_t(parsed.splat_w) * size_t(parsed.splat_h);
    if (!has_pf99 || parsed.chunk_w == 0 || parsed.chunk_h == 0 ||
        parsed.lods.empty()) {
        return false;
    }

    const int CW = int(parsed.chunk_w);
    const int CH = int(parsed.chunk_h);
    const int N =
        std::min<int>(std::max<int>(1, int(parsed.lods.size())), 32);
    const int W = CW * 32 + 1;
    const int H = CH * 32 + 1;
    const size_t area = size_t(W) * size_t(H);
    std::vector<float> weights(size_t(N) * area, 0.0f);

    const auto mask_sample = [&](const EhfChunkLayer& layer,
                                 int px,
                                 int py) -> float
    {
        const std::vector<uint8_t>* map = &parsed.splat_indices;
        int mw = int(parsed.splat_w);
        int mh = int(parsed.splat_h);
        if (size_t(layer.name_idx) < parsed.paint_resources.size()) {
            const auto& pr = parsed.paint_resources[size_t(layer.name_idx)];
            if (!pr.data.empty() && pr.width > 0 && pr.height > 0) {
                map = &pr.data;
                mw  = int(pr.width);
                mh  = int(pr.height);
            }
        }
        if (map->empty() || mw <= 0 || mh <= 0) return 1.0f;
        const int ox = std::clamp(
            int(std::floor(layer.tile_uv[0] * float(mw))), 0, mw - 1);
        const int oy = std::clamp(
            int(std::floor(layer.tile_uv[1] * float(mh))), 0, mh - 1);
        const int sx = std::clamp(ox + px, 0, mw - 1);
        const int sy = std::clamp(oy + py, 0, mh - 1);
        return float((*map)[size_t(sy) * size_t(mw) + size_t(sx)]) / 255.0f;
    };

    const size_t expected_chunks = size_t(CW) * size_t(CH);
    for (size_t ci = 0;
         ci < parsed.chunks.size() && ci < expected_chunks;
         ++ci) {
        const int cx = int(ci / size_t(CH));
        const int cy = int(ci % size_t(CH));
        const auto& chunk = parsed.chunks[ci];
        const int layer_count =
            std::min<int>(int(chunk.layers.size()), 16);

        for (int li = 0; li < layer_count; ++li) {
            const auto& layer = chunk.layers[size_t(li)];
            const int mat =
                std::clamp<int>(int(layer.material_idx), 0, N - 1);
            for (int py = 0; py <= 32; ++py) {
                for (int px = 0; px <= 32; ++px) {
                    const float layer_w = mask_sample(layer, px, py);
                    if (layer_w <= 0.0001f) continue;

                    const int gx = cx * 32 + px;
                    const int gy = cy * 32 + py;
                    if (gx < 0 || gy < 0 || gx >= W || gy >= H) continue;
                    weights[size_t(mat) * area +
                            size_t(gy) * size_t(W) + size_t(gx)] += layer_w;
                }
            }
        }
    }

    for (size_t p = 0; p < area; ++p) {
        float sum = 0.0f;
        for (int m = 0; m < N; ++m) {
            sum += weights[size_t(m) * area + p];
        }
        if (sum > 0.00001f) {
            const float inv = 1.0f / sum;
            for (int m = 0; m < N; ++m) {
                weights[size_t(m) * area + p] *= inv;
            }
        } else {
            weights[p] = 1.0f;
        }
    }

    out.width = W;
    out.height = H;
    out.material_count = N;
    out.rgba.resize(size_t(N));
    for (int m = 0; m < N; ++m) {
        auto& img = out.rgba[size_t(m)];
        img.resize(area * 4);
        for (size_t p = 0; p < area; ++p) {
            const float w =
                std::clamp(weights[size_t(m) * area + p], 0.0f, 1.0f);
            const uint8_t b = uint8_t(std::lround(w * 255.0f));
            img[p * 4 + 0] = b;
            img[p * 4 + 1] = b;
            img[p * 4 + 2] = b;
            img[p * 4 + 3] = b;
        }
    }
    return true;
}
