uint32_t gdb_model_path_hash(std::string path) {
    std::transform(path.begin(), path.end(), path.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    std::replace(path.begin(), path.end(), '/', '\\');

    uint32_t h = 0x811C9DC5u;
    for (unsigned char c : path) {
        h *= 0x01000193u;
        h ^= uint32_t(c);
    }
    return h;
}

uint64_t model_animation_binding_revision() {
    return g_model_animation_binding_revision;
}

size_t build_model_animation_cache_for_hash(
    uint32_t model_path_hash,
    size_t clip_count,
    std::vector<uint8_t>& out_authored) {
    out_authored.assign(clip_count, 0);
    if (model_path_hash == 0 || clip_count == 0) return 0;

    size_t count = 0;
    for (const ModelAnimationBinding& binding :
         g_model_animation_bindings) {
        if (binding.model_path_hash != model_path_hash ||
            binding.clip_index >= clip_count) {
            continue;
        }
        if (!out_authored[binding.clip_index]) {
            out_authored[binding.clip_index] = 1;
            ++count;
        }
    }
    return count;
}

size_t model_animation_binding_count_for_hash(uint32_t model_path_hash) {
    if (model_path_hash == 0) return 0;
    size_t count = 0;
    for (const ModelAnimationBinding& binding :
         g_model_animation_bindings) {
        if (binding.model_path_hash == model_path_hash) ++count;
    }
    return count;
}

const std::vector<ModelAnimationBinding>& model_animation_bindings() {
    return g_model_animation_bindings;
}
