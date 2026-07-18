void save_clip_name_cache_for_root(const std::string& root,
                                   const std::vector<AnimClip>& clips) {
    if (clips.empty()) return;
    std::vector<uint8_t> out;
    out.reserve(clips.size() * 24 +
                g_model_animation_bindings.size() * 48 + 64);
    cache_put_u32(out, kClipCacheMagic);
    cache_put_u32(out, kClipCacheVersion);
    const uint64_t fp = clip_cache_fingerprint(root, clips);
    cache_put_u32(out, uint32_t(fp));
    cache_put_u32(out, uint32_t(fp >> 32));
    cache_put_u32(out, (uint32_t)clips.size());
    for (const AnimClip& c : clips) {
        cache_put_u32(out, c.key0);
        cache_put_str(out, c.name);
    }
    cache_put_u32(out, (uint32_t)g_model_animation_bindings.size());
    for (const ModelAnimationBinding& b : g_model_animation_bindings) {
        cache_put_u32(out, b.model_path_hash);
        cache_put_u32(out, b.skeleton_file_hash);
        cache_put_u32(out, b.retarget_skeleton_file_hash);
        cache_put_u32(out, b.animation_record_hash);
        cache_put_u32(out, b.animation_key);
        cache_put_u32(out, b.source_record_hash);
        cache_put_u32(out, (uint32_t)b.clip_index);
        cache_put_str(out, b.animation_name);
        cache_put_str(out, b.source_name);
    }
    std::ofstream f(clip_cache_path(), std::ios::binary | std::ios::trunc);
    if (!f) return;
    f.write(reinterpret_cast<const char*>(out.data()),
            (std::streamsize)out.size());
}
