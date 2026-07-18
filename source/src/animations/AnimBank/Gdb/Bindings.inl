void append_model_animation_binding(uint32_t model_hash,
                                    uint32_t skeleton_hash,
                                    uint32_t retarget_hash,
                                    uint32_t source_record_hash,
                                    const GdbClipRef& ref,
                                    const AnimClip& clip,
                                    GdbAnimScanStats& stats) {
    for (const ModelAnimationBinding& existing :
         g_model_animation_bindings) {
        if (existing.model_path_hash == model_hash &&
            existing.clip_index == ref.clip_index &&
            existing.animation_key == ref.animation_key) {
            return;
        }
    }

    ModelAnimationBinding binding;
    binding.model_path_hash = model_hash;
    binding.skeleton_file_hash = skeleton_hash;
    binding.retarget_skeleton_file_hash = retarget_hash;
    binding.animation_record_hash = ref.animation_record_hash;
    binding.animation_key = ref.animation_key;
    binding.source_record_hash = source_record_hash;
    binding.clip_index = ref.clip_index;
    binding.animation_name = !ref.label.empty() ? ref.label : clip.name;
    binding.source_name = ref.label;
    g_model_animation_bindings.push_back(std::move(binding));
    ++stats.model_binding_refs;
    stats.model_binding_models.insert(model_hash);
}

void scan_gdb_animation_fields(
    const std::vector<uint8_t>& bytes,
    const std::unordered_map<uint32_t, size_t>& by_key0,
    std::vector<AnimClip>& clips,
    GdbAnimScanStats& stats) {
    constexpr uint32_t kHashAnimationName = 0x78B1F79Cu;
    constexpr uint32_t kHashAnimName = 0x49BD6FC7u;
    constexpr uint32_t kHashAnimation = 0x8F32748Du;

    ++stats.files_seen;
    GdbMiniView view(bytes);
    if (!view.ok) return;
    ++stats.files_parsed;

    std::unordered_map<uint32_t, GdbAnimScanStats::AnimationRecord>
        records_in_this_gdb;
    records_in_this_gdb.reserve(view.record_offsets.size() / 4 + 1);
    const auto children_by_parent_hash = build_children_by_parent_hash(view);

    for (size_t i = 0; i < view.record_offsets.size(); ++i) {
        const size_t record = view.record_offsets[i];
        const uint32_t record_hash = view.record_hash(i);

        uint32_t raw = 0;
        uint32_t animation_owner_hash = 0;
        if (view.find_field(record, kHashAnimation, 4, raw,
                            &animation_owner_hash)) {
            const bool is_local_animation = animation_owner_hash == record_hash;
            if (is_local_animation) {
                ++stats.animation_fields;
            } else {
                ++stats.inherited_animation_fields;
            }
            auto hit = by_key0.find(raw);
            if (hit != by_key0.end()) {
                ++stats.animation_key_hits;
                stats.unique_clip_hits.insert(raw);
                std::string label = label_for_gdb_animation_source(
                    view, record_hash, raw);
                GdbAnimScanStats::AnimationRecord rec{
                    hit->second, raw, label};
                stats.animation_records.emplace(record_hash, rec);
                records_in_this_gdb.emplace(record_hash, std::move(rec));

                AnimClip& clip = clips[hit->second];
                if (is_local_animation &&
                    assign_gdb_clip_label(clip, label)) {
                    ++stats.names_assigned;
                }
            }
        }

        raw = 0;
        if (view.find_local(record, kHashAnimationName, 4, raw) ||
            view.find_local(record, kHashAnimName, 4, raw)) {
            ++stats.selector_fields;
            if (by_key0.find(raw) != by_key0.end()) {
                ++stats.selector_key_hits;
            }
            stats.selectors.push_back(
                GdbAnimScanStats::SelectorRecord{
                    raw,
                    label_for_gdb_animation_source(view, record_hash, raw)});
        }
    }

    std::vector<GdbMiniView::Field> fields;
    for (size_t i = 0; i < view.record_offsets.size(); ++i) {
        const size_t record = view.record_offsets[i];
        if (!view.local_fields(record, fields)) continue;
        for (const GdbMiniView::Field& field : fields) {
            if (field.type == 6) {
                auto rec_it = records_in_this_gdb.find(field.raw);
                if (rec_it == records_in_this_gdb.end()) continue;
                std::string label = name_for_gdb_hash(view, field.hash);
                if (is_generic_animation_field_name(field.hash, label)) {
                    continue;
                }
                ++stats.reference_record_hits;
                AnimClip& clip = clips[rec_it->second.clip_index];
                if (assign_gdb_clip_label(clip, label)) {
                    ++stats.reference_names_assigned;
                }
            } else if (field.type == 4) {
                auto hit = by_key0.find(field.raw);
                if (hit == by_key0.end()) continue;
                std::string label = name_for_gdb_hash(view, field.hash);
                if (is_generic_animation_field_name(field.hash, label)) {
                    continue;
                }
                ++stats.reference_key_hits;
                AnimClip& clip = clips[hit->second];
                if (assign_gdb_clip_label(clip, label)) {
                    ++stats.reference_names_assigned;
                }
            }
        }
    }

    for (size_t i = 0; i < view.record_offsets.size(); ++i) {
        const size_t record = view.record_offsets[i];
        const uint32_t record_hash = view.record_hash(i);

        std::vector<uint32_t> model_hashes;
        collect_animated_model_hashes_for_record(view, record, model_hashes);
        if (model_hashes.empty()) continue;

        std::vector<GdbClipRef> refs;
        collect_clip_refs_for_record(view, record, children_by_parent_hash,
                                     records_in_this_gdb, by_key0, refs);
        if (refs.empty()) continue;

        uint32_t skeleton_hash = 0;
        uint32_t retarget_hash = 0;
        constexpr uint32_t kHashSkeletonFile = 0xC3D06E3Au;
        constexpr uint32_t kHashRetargetSkeletonFile = 0x64234AF2u;
        find_inherited_hash_any(view, record, kHashSkeletonFile,
                                skeleton_hash);
        find_inherited_hash_any(view, record, kHashRetargetSkeletonFile,
                                retarget_hash);

        for (uint32_t model_hash : model_hashes) {
            for (const GdbClipRef& ref : refs) {
                if (ref.clip_index >= clips.size()) continue;
                append_model_animation_binding(
                    model_hash, skeleton_hash, retarget_hash, record_hash,
                    ref, clips[ref.clip_index], stats);
            }
        }
    }
}
