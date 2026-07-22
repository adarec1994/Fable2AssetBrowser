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

size_t build_animation_cache_for_scope(
    uint32_t source_record_hash,
    const std::vector<uint32_t>& model_path_hashes,
    size_t clip_count,
    std::vector<uint8_t>& out_authored,
    std::vector<size_t>& out_order,
    std::vector<std::string>& out_names) {
    out_authored.assign(clip_count, 0);
    out_order.clear();
    out_names.assign(clip_count, std::string());
    if (clip_count == 0 ||
        (source_record_hash == 0 && model_path_hashes.empty())) {
        return 0;
    }

    auto append = [&](const ModelAnimationBinding& binding) {
        if (binding.clip_index >= clip_count) return;
        if (!out_authored[binding.clip_index]) {
            out_authored[binding.clip_index] = 1;
            out_order.push_back(binding.clip_index);
        }
        if (out_names[binding.clip_index].empty() &&
            !binding.animation_name.empty()) {
            out_names[binding.clip_index] = binding.animation_name;
        }
    };




    if (source_record_hash != 0) {
        for (const ModelAnimationBinding& binding :
             g_model_animation_bindings) {
            if (binding.source_record_hash == source_record_hash) {
                append(binding);
            }
        }
        if (!out_order.empty()) return out_order.size();
    }



    std::unordered_set<uint32_t> model_hashes(
        model_path_hashes.begin(), model_path_hashes.end());
    for (const ModelAnimationBinding& binding :
         g_model_animation_bindings) {
        if (model_hashes.find(binding.model_path_hash) !=
            model_hashes.end()) {
            append(binding);
        }
    }
    return out_order.size();
}

uint64_t animation_scope_signature(
    uint32_t source_record_hash,
    const std::vector<uint32_t>& model_path_hashes) {
    uint64_t hash = 1469598103934665603ull;
    auto mix = [&](uint32_t value) {
        for (int byte = 0; byte < 4; ++byte) {
            hash ^= (value >> (byte * 8)) & 0xFFu;
            hash *= 1099511628211ull;
        }
    };
    mix(source_record_hash);
    for (uint32_t model_hash : model_path_hashes) mix(model_hash);
    return hash ? hash : 1;
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
