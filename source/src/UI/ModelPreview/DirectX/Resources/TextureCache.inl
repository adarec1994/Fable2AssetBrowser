namespace {
struct TexCacheKey {
    std::string name_lower;
    std::string preferred_bnk;
    bool operator==(const TexCacheKey& o) const {
        return name_lower == o.name_lower && preferred_bnk == o.preferred_bnk;
    }
};
struct TexCacheKeyHash {
    size_t operator()(const TexCacheKey& k) const noexcept {
        return std::hash<std::string>{}(k.name_lower)
             ^ (std::hash<std::string>{}(k.preferred_bnk) << 1);
    }
};
struct TexCacheEntry {
#ifdef _WIN32
    ID3D11ShaderResourceView* srv = nullptr;
#endif
    bool has_alpha = false;
    bool tried     = false;
};

std::mutex& tex_cache_mutex() {
    static std::mutex m;
    return m;
}
std::unordered_map<TexCacheKey, TexCacheEntry, TexCacheKeyHash>& tex_cache_table() {
    static std::unordered_map<TexCacheKey, TexCacheEntry, TexCacheKeyHash> t;
    return t;
}
}

void MP_TextureCache_Clear() {
#ifdef _WIN32
    std::lock_guard<std::mutex> lk(tex_cache_mutex());
    for (auto& kv : tex_cache_table()) {
        if (kv.second.srv) kv.second.srv->Release();
    }
    tex_cache_table().clear();
#endif
}
