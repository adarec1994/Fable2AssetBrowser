bool load_clip_name_cache_for_root(const std::string& root,
                                   std::vector<AnimClip>& clips) {
    if (clips.empty()) return false;
    std::ifstream f(clip_cache_path(), std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamoff len = f.tellg();
    if (len < 20) return false;
    std::vector<uint8_t> bytes((size_t)len);
    f.seekg(0);
    if (!f.read(reinterpret_cast<char*>(bytes.data()), len)) return false;

    CacheReader r{bytes.data(), bytes.size()};
    if (r.u32() != kClipCacheMagic) return false;
    if (r.u32() != kClipCacheVersion) return false;
    if (r.u64() != clip_cache_fingerprint(root, clips)) return false;
    const uint32_t clip_count = r.u32();
    if (!r.ok || clip_count != clips.size()) return false;

    std::vector<std::string> names(clip_count);
    for (uint32_t i = 0; i < clip_count; ++i) {
        const uint32_t key0 = r.u32();
        names[i] = r.str();
        if (!r.ok || key0 != clips[i].key0) return false;
    }
    const uint32_t binding_count = r.u32();
    if (!r.ok) return false;
    std::vector<ModelAnimationBinding> bindings;
    bindings.reserve(binding_count);
    for (uint32_t i = 0; i < binding_count; ++i) {
        ModelAnimationBinding b;
        b.model_path_hash = r.u32();
        b.skeleton_file_hash = r.u32();
        b.retarget_skeleton_file_hash = r.u32();
        b.animation_record_hash = r.u32();
        b.animation_key = r.u32();
        b.source_record_hash = r.u32();
        b.clip_index = r.u32();
        b.animation_name = r.str();
        b.source_name = r.str();
        if (!r.ok || b.clip_index >= clips.size()) return false;
        bindings.push_back(std::move(b));
    }

    for (uint32_t i = 0; i < clip_count; ++i) {
        if (!names[i].empty()) clips[i].name = std::move(names[i]);
    }
    g_model_animation_bindings = std::move(bindings);
    ++g_model_animation_binding_revision;
    if (g_model_animation_binding_revision == 0) {
        g_model_animation_binding_revision = 1;
    }
    OutputLog::success(
        "animation names restored from cache (" +
        std::to_string(clip_count) + " clips, " +
        std::to_string(binding_count) + " model bindings)");
    return true;
}
