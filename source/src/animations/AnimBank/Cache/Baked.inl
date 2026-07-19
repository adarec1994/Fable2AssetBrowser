#include "BakedNames.inl"

// Applies the baked (compiled-in) clip-name + model-binding table. This is
// the fully resolved retail data, so load never has to re-scan the GDBs -
// the disk cache and the scan remain only as fallbacks for foreign data.
bool load_clip_names_from_baked(std::vector<AnimClip>& clips) {
    if (clips.empty()) return false;

    std::vector<uint8_t> payload(kBakedAnimNamesRawSize);
    uLongf out_len = (uLongf)payload.size();
    if (uncompress(payload.data(), &out_len, kBakedAnimNamesDeflate,
                   (uLong)sizeof(kBakedAnimNamesDeflate)) != Z_OK ||
        out_len != kBakedAnimNamesRawSize) {
        return false;
    }

    CacheReader r{payload.data(), payload.size()};
    const uint32_t clip_count = r.u32();
    if (!r.ok || clip_count == 0) return false;

    std::vector<std::pair<uint32_t, std::string>> ordered;
    ordered.reserve(clip_count);
    std::unordered_map<uint32_t, const std::string*> by_key;
    by_key.reserve(clip_count * 2);
    for (uint32_t i = 0; i < clip_count; ++i) {
        const uint32_t key0 = r.u32();
        std::string name = r.str();
        if (!r.ok) return false;
        ordered.emplace_back(key0, std::move(name));
    }
    for (const auto& entry : ordered) {
        by_key[entry.first] = &entry.second;
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
        if (!r.ok) return false;
        if (b.clip_index < clips.size()) bindings.push_back(std::move(b));
    }

    bool positional = clip_count == clips.size();
    if (positional) {
        for (uint32_t i = 0; i < clip_count; ++i) {
            if (ordered[i].first != clips[i].key0) {
                positional = false;
                break;
            }
        }
    }

    size_t applied = 0;
    if (positional) {
        for (uint32_t i = 0; i < clip_count; ++i) {
            if (!ordered[i].second.empty()) {
                clips[i].name = ordered[i].second;
                ++applied;
            }
        }
    } else {
        for (AnimClip& c : clips) {
            auto it = by_key.find(c.key0);
            if (it != by_key.end() && !it->second->empty()) {
                c.name = *it->second;
                ++applied;
            }
        }
        // A mostly-missed table means foreign game data: let the caller
        // fall back to the cache/scan path. Binding clip indices are
        // positional, so they are only trustworthy on the exact list.
        if (applied * 2 < clips.size()) return false;
        bindings.clear();
    }

    g_model_animation_bindings = std::move(bindings);
    ++g_model_animation_binding_revision;
    if (g_model_animation_binding_revision == 0) {
        g_model_animation_binding_revision = 1;
    }
    OutputLog::success(
        "animation names applied from baked table (" +
        std::to_string(applied) + " clips, " +
        std::to_string(g_model_animation_bindings.size()) +
        " model bindings)");
    return true;
}

// Writes the resolved names + bindings next to the executable so they can
// be inspected (and re-baked with tools/gen_baked_anim.py when desired).
void dump_resolved_clip_names_txt(const std::vector<AnimClip>& clips) {
    std::ofstream f(std::filesystem::current_path() /
                        "anim_names_resolved.txt",
                    std::ios::trunc);
    if (!f) return;
    char hex[16];
    f << "# key0(hex)\tname\n";
    for (const AnimClip& c : clips) {
        std::snprintf(hex, sizeof(hex), "%08x", c.key0);
        f << hex << '\t' << c.name << '\n';
    }
    f << "# bindings: " << g_model_animation_bindings.size() << '\n';
    for (const ModelAnimationBinding& b : g_model_animation_bindings) {
        const uint32_t vals[7] = {
            b.model_path_hash,       b.skeleton_file_hash,
            b.retarget_skeleton_file_hash, b.animation_record_hash,
            b.animation_key,         b.source_record_hash,
            (uint32_t)b.clip_index};
        f << "bind";
        for (uint32_t v : vals) {
            std::snprintf(hex, sizeof(hex), "%08x", v);
            f << '\t' << hex;
        }
        f << '\t' << b.animation_name << '\t' << b.source_name << '\n';
    }
}
