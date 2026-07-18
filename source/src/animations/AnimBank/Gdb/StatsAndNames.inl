struct GdbAnimScanStats {
    struct AnimationRecord {
        size_t clip_index = 0;
        uint32_t anim_hash = 0;
        std::string label;
    };

    struct SelectorRecord {
        uint32_t selector_hash = 0;
        std::string label;
    };

    size_t files_seen = 0;
    size_t files_parsed = 0;
    size_t animation_fields = 0;
    size_t inherited_animation_fields = 0;
    size_t animation_key_hits = 0;
    size_t selector_fields = 0;
    size_t selector_key_hits = 0;
    size_t selector_record_hits = 0;
    size_t selector_names_assigned = 0;
    size_t reference_record_hits = 0;
    size_t reference_key_hits = 0;
    size_t reference_names_assigned = 0;
    size_t bnks_seen = 0;
    size_t bnk_gdb_entries = 0;
    size_t bnk_nested_seen = 0;
    size_t model_binding_refs = 0;
    std::unordered_set<uint32_t> model_binding_models;
    size_t names_assigned = 0;
    std::unordered_set<uint32_t> unique_clip_hits;
    std::unordered_map<uint32_t, AnimationRecord> animation_records;
    std::vector<SelectorRecord> selectors;
};

std::string name_for_gdb_hash(const GdbMiniView& view, uint32_t hash) {
    auto str_it = view.strings.find(hash);
    if (str_it != view.strings.end() && !str_it->second.empty()) {
        return str_it->second;
    }

    const auto& enum_names = external_gdb_enum_names();
    auto enum_it = enum_names.find(hash);
    if (enum_it != enum_names.end() && !enum_it->second.empty()) {
        return enum_it->second;
    }

    return {};
}

std::string label_for_gdb_animation_source(const GdbMiniView& view,
                                           uint32_t record_hash,
                                           uint32_t anim_hash) {
    std::string name = name_for_gdb_hash(view, anim_hash);
    if (!name.empty()) return name;

    name = name_for_gdb_hash(view, record_hash);
    if (!name.empty()) return name;

    return "gdb_" + hex8(record_hash);
}

bool is_generic_animation_field_name(uint32_t field_hash,
                                     const std::string& label) {
    constexpr uint32_t kHashParent = 0x5F6317D5u;
    constexpr uint32_t kHashAnimationName = 0x78B1F79Cu;
    constexpr uint32_t kHashAnimName = 0x49BD6FC7u;
    constexpr uint32_t kHashAnimation = 0x8F32748Du;
    constexpr uint32_t kHashAnimations = 0xF96D7984u;
    if (field_hash == kHashParent ||
        field_hash == kHashAnimationName ||
        field_hash == kHashAnimName ||
        field_hash == kHashAnimation ||
        field_hash == kHashAnimations) {
        return true;
    }
    return label.empty() ||
           label == "Animation" ||
           label == "Animations" ||
           label == "AnimationName" ||
           label == "AnimName" ||
           label == "parent";
}

bool assign_gdb_clip_label(AnimClip& clip, const std::string& label) {
    if (label.empty()) return false;
    if (!is_default_clip_name(clip) && !is_gdb_fallback_name(clip)) {
        return false;
    }
    clip.name = label;
    return true;
}

void push_unique_u32(std::vector<uint32_t>& out, uint32_t value) {
    if (value == 0 || value == 0x811C9DC5u) return;
    if (std::find(out.begin(), out.end(), value) == out.end()) {
        out.push_back(value);
    }
}

bool find_inherited_hash_any(const GdbMiniView& view,
                             size_t record,
                             uint32_t field_hash,
                             uint32_t& raw) {
    std::unordered_set<size_t> visited;
    size_t cur = record;
    std::vector<GdbMiniView::Field> fields;
    for (int depth = 0; depth < 64; ++depth) {
        if (!visited.insert(cur).second) return false;
        if (!view.local_fields(cur, fields)) return false;
        for (const GdbMiniView::Field& field : fields) {
            if (field.hash != field_hash) continue;
            if (field.type != 3 && field.type != 4 && field.type != 6 &&
                field.type != 7) {
                continue;
            }
            raw = field.raw;
            return raw != 0 && raw != 0x811C9DC5u;
        }

        uint32_t parent_hash = 0;
        if (!view.find_local(cur, GdbMiniView::kHashParent, 6,
                             parent_hash) || parent_hash == 0) {
            return false;
        }

        size_t parent_record = 0;
        if (!view.lookup(parent_hash, parent_record) ||
            parent_record == cur) {
            return false;
        }
        cur = parent_record;
    }
    return false;
}
