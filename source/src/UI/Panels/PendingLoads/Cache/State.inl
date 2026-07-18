struct CachedPropModel {
    bool loaded = false;
    MDLInfo info;
    std::vector<MDLMeshGeom> geoms;
};

struct CachedTerrainTextureRgba {
    std::vector<uint8_t> rgba;
    int width = 0;
    int height = 0;
};

std::unordered_map<std::string, CachedTerrainTextureRgba>& terrain_lod_rgba_cache()
{
    static std::unordered_map<std::string, CachedTerrainTextureRgba> cache;
    return cache;
}

size_t& terrain_lod_rgba_cache_bytes()
{
    static size_t bytes = 0;
    return bytes;
}

void remember_terrain_lod_rgba(std::string key,
                               std::vector<uint8_t> rgba,
                               int width,
                               int height)
{
    if (rgba.empty() || width <= 0 || height <= 0) return;
    auto& cache = terrain_lod_rgba_cache();
    auto& bytes = terrain_lod_rgba_cache_bytes();
    constexpr size_t kMaxTerrainLodCacheBytes = 96ull * 1024ull * 1024ull;
    if (bytes + rgba.size() > kMaxTerrainLodCacheBytes) {
        cache.clear();
        bytes = 0;
    }
    auto it = cache.find(key);
    if (it != cache.end()) {
        bytes -= it->second.rgba.size();
    }
    bytes += rgba.size();
    cache[std::move(key)] = CachedTerrainTextureRgba{
        std::move(rgba), width, height
    };
}
