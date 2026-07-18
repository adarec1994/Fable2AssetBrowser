std::unordered_map<uint32_t, std::vector<size_t>>
build_children_by_parent_hash(const GdbMiniView& view) {
    std::unordered_map<uint32_t, std::vector<size_t>> out;
    for (size_t i = 0; i < view.record_offsets.size(); ++i) {
        const size_t record = view.record_offsets[i];
        uint32_t parent_hash = 0;
        if (!view.find_local(record, GdbMiniView::kHashParent, 6,
                             parent_hash) || parent_hash == 0) {
            continue;
        }
        out[parent_hash].push_back(record);
    }
    return out;
}

struct GdbClipRef {
    size_t clip_index = 0;
    uint32_t animation_record_hash = 0;
    uint32_t animation_key = 0;
    std::string label;
};

void push_unique_clip_ref(std::vector<GdbClipRef>& out,
                          GdbClipRef ref) {
    for (const GdbClipRef& existing : out) {
        if (existing.clip_index == ref.clip_index &&
            existing.animation_key == ref.animation_key) {
            return;
        }
    }
    out.push_back(std::move(ref));
}

void collect_clip_refs_from_animation_group(
    const GdbMiniView& view,
    uint32_t group_hash,
    const std::unordered_map<uint32_t, std::vector<size_t>>& children,
    const std::unordered_map<uint32_t,
                             GdbAnimScanStats::AnimationRecord>& records,
    const std::unordered_map<uint32_t, size_t>& by_key0,
    std::vector<GdbClipRef>& out) {
    size_t group_record = 0;
    if (!view.lookup(group_hash, group_record)) return;

    std::vector<size_t> stack;
    std::unordered_set<size_t> visited;
    stack.push_back(group_record);
    std::vector<GdbMiniView::Field> fields;
    while (!stack.empty()) {
        const size_t record = stack.back();
        stack.pop_back();
        if (!visited.insert(record).second) continue;

        if (view.local_fields(record, fields)) {
            for (const GdbMiniView::Field& field : fields) {
                std::string label = name_for_gdb_hash(view, field.hash);
                if (is_generic_animation_field_name(field.hash, label)) {
                    continue;
                }

                if (field.type == 6) {
                    auto rec_it = records.find(field.raw);
                    if (rec_it == records.end()) continue;
                    push_unique_clip_ref(
                        out,
                        GdbClipRef{rec_it->second.clip_index,
                                   field.raw,
                                   rec_it->second.anim_hash,
                                   label});
                } else if (field.type == 4) {
                    auto key_it = by_key0.find(field.raw);
                    if (key_it == by_key0.end()) continue;
                    push_unique_clip_ref(
                        out,
                        GdbClipRef{key_it->second, 0, field.raw, label});
                }
            }
        }

        const uint32_t record_hash = view.hash_for_record(record);
        auto child_it = children.find(record_hash);
        if (child_it == children.end()) continue;
        for (size_t child : child_it->second) {
            stack.push_back(child);
        }
    }
}

void collect_clip_refs_for_record(
    const GdbMiniView& view,
    size_t record,
    const std::unordered_map<uint32_t, std::vector<size_t>>& children,
    const std::unordered_map<uint32_t,
                             GdbAnimScanStats::AnimationRecord>& records,
    const std::unordered_map<uint32_t, size_t>& by_key0,
    std::vector<GdbClipRef>& out) {
    constexpr uint32_t kHashAnimation = 0x8F32748Du;
    constexpr uint32_t kHashAnimationList = 0x63565E85u;
    constexpr uint32_t kHashAnimations = 0xF96D7984u;
    constexpr uint32_t kHashAnimationSet = 0xE227399Fu;
    constexpr uint32_t kHashAnimationManagerComponent = 0x7C63CDDFu;

    auto collect_scope = [&](size_t scope_record) {
        uint32_t raw = 0;
        if (find_inherited_hash_any(view, scope_record, kHashAnimation,
                                    raw)) {
            auto key_it = by_key0.find(raw);
            if (key_it != by_key0.end()) {
                push_unique_clip_ref(
                    out,
                    GdbClipRef{
                        key_it->second, view.hash_for_record(scope_record),
                        raw,
                        label_for_gdb_animation_source(
                            view, view.hash_for_record(scope_record), raw)});
            }
        }

        const uint32_t group_fields[] = {
            kHashAnimationList, kHashAnimations, kHashAnimationSet
        };
        for (uint32_t field_hash : group_fields) {
            raw = 0;
            if (!find_inherited_hash_any(view, scope_record, field_hash,
                                         raw)) {
                continue;
            }
            collect_clip_refs_from_animation_group(
                view, raw, children, records, by_key0, out);
        }
    };

    collect_scope(record);

    uint32_t manager_hash = 0;
    if (find_inherited_hash_any(view, record,
                                kHashAnimationManagerComponent,
                                manager_hash)) {
        size_t manager_record = 0;
        if (view.lookup(manager_hash, manager_record) &&
            manager_record != record) {
            collect_scope(manager_record);
        }
    }
}
