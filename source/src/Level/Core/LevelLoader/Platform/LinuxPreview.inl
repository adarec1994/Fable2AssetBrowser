#ifndef _WIN32
static Level::GhfHeights make_linux_preview_heightfield(
    const Level::GhfHeights& source, size_t max_vertices)
{
    const size_t source_vertices =
        size_t(source.width) * size_t(source.height);
    if (source_vertices <= max_vertices || max_vertices < 4)
        return source;

    const double ratio =
        std::sqrt(double(source_vertices) / double(max_vertices));
    const uint32_t step =
        std::max<uint32_t>(2, uint32_t(std::ceil(ratio)));
    const uint32_t preview_w = (source.width - 1 + step - 1) / step + 1;
    const uint32_t preview_h = (source.height - 1 + step - 1) / step + 1;

    Level::GhfHeights preview;
    preview.ok = true;
    preview.width = preview_w;
    preview.height = preview_h;
    preview.tile_size = source.tile_size * float(step);
    preview.min_height = std::numeric_limits<float>::infinity();
    preview.max_height = -std::numeric_limits<float>::infinity();
    preview.heights.resize(size_t(preview_w) * size_t(preview_h));

    for (uint32_t y = 0; y < preview_h; ++y) {
        const uint32_t sy = std::min(y * step, source.height - 1);
        for (uint32_t x = 0; x < preview_w; ++x) {
            const uint32_t sx = std::min(x * step, source.width - 1);
            const float height =
                source.heights[size_t(sy) * source.width + sx];
            preview.heights[size_t(y) * preview_w + x] = height;
            preview.min_height = std::min(preview.min_height, height);
            preview.max_height = std::max(preview.max_height, height);
        }
    }

    OutputLog::info(
        "Linux low-memory terrain preview: " +
        std::to_string(source.width) + "x" + std::to_string(source.height) +
        " -> " + std::to_string(preview.width) + "x" +
        std::to_string(preview.height) + " (sample step " +
        std::to_string(step) + ")");
    return preview;
}
#endif
