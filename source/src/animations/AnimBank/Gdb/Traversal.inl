void visit_record_and_parents(
    const GdbMiniView& view,
    size_t record,
    const std::function<void(size_t)>& fn) {
    std::unordered_set<size_t> visited;
    size_t cur = record;
    for (int depth = 0; depth < 64; ++depth) {
        if (!visited.insert(cur).second) return;
        fn(cur);

        uint32_t parent_hash = 0;
        if (!view.find_local(cur, GdbMiniView::kHashParent, 6,
                             parent_hash) || parent_hash == 0) {
            return;
        }

        size_t parent_record = 0;
        if (!view.lookup(parent_hash, parent_record) ||
            parent_record == cur) {
            return;
        }
        cur = parent_record;
    }
}

void collect_model_file_hashes_from_record(
    const GdbMiniView& view,
    size_t record,
    std::vector<uint32_t>& out) {
    constexpr uint32_t kHashModelFile = 0x0C17DB4Eu;
    constexpr uint32_t kHashModelFile1 = 0x578E3BFBu;
    constexpr uint32_t kHashModelFile2 = 0x578E3BF8u;

    std::vector<GdbMiniView::Field> fields;
    visit_record_and_parents(view, record, [&](size_t cur) {
        if (!view.local_fields(cur, fields)) return;
        for (const GdbMiniView::Field& field : fields) {
            if (field.type != 3 && field.type != 4 && field.type != 7) {
                continue;
            }
            if (field.hash == kHashModelFile ||
                field.hash == kHashModelFile1 ||
                field.hash == kHashModelFile2) {
                push_unique_u32(out, field.raw);
            }
        }
    });
}

void collect_animated_model_hashes_for_record(
    const GdbMiniView& view,
    size_t record,
    std::vector<uint32_t>& out) {
    constexpr uint32_t kHashGraphicAppearanceComponent = 0xA7B6EF56u;
    constexpr uint32_t kHashGraphicAppearanceAnimatedMeshComponent =
        0x21D312CAu;
    constexpr uint32_t kHashGraphicAppearanceMorphComponent =
        0x0D4ADA1Au;
    constexpr uint32_t kHashCompositeModelRecord = 0x7CFF5EE2u;
    constexpr uint32_t kHashModel = 0x90347E14u;

    auto collect_component = [&](uint32_t component_hash) {
        size_t component_record = 0;
        if (!view.lookup(component_hash, component_record)) return;
        collect_model_file_hashes_from_record(view, component_record, out);
    };

    uint32_t direct_component_hash = 0;
    if (find_inherited_hash_any(view, record,
                                kHashGraphicAppearanceAnimatedMeshComponent,
                                direct_component_hash)) {
        collect_component(direct_component_hash);
    }

    uint32_t appearance_hash = 0;
    if (find_inherited_hash_any(view, record,
                                kHashGraphicAppearanceComponent,
                                appearance_hash)) {
        size_t appearance_record = 0;
        if (view.lookup(appearance_hash, appearance_record)) {
            visit_record_and_parents(view, appearance_record,
                                     [&](size_t cur) {
                uint32_t component_hash = 0;
                if (view.find_local(
                        cur, kHashGraphicAppearanceAnimatedMeshComponent,
                        6, component_hash)) {
                    collect_component(component_hash);
                }
            });
        }
    }

    uint32_t morph_hash = 0;
    if (!find_inherited_hash_any(view, record,
                                 kHashGraphicAppearanceMorphComponent,
                                 morph_hash)) {
        return;
    }
    size_t morph_record = 0;
    if (!view.lookup(morph_hash, morph_record)) return;
    uint32_t composite_hash = 0;
    if (!find_inherited_hash_any(view, morph_record,
                                 kHashCompositeModelRecord,
                                 composite_hash)) {
        return;
    }
    size_t composite_record = 0;
    if (!view.lookup(composite_hash, composite_record)) return;






    const auto is_model_field = [](uint32_t hash) {
        switch (hash) {
        case 0x90347E14u:
        case 0x0C17DB4Eu:
        case 0x578E3BFBu:
        case 0x578E3BF8u:
        case 0x1372D766u:
        case 0x05294B89u:
        case 0x547DDA3Eu:
        case 0xD622E5ADu:
        case 0x139ADC04u:
        case 0x8D09F54Fu:
        case 0x8D09F551u:
        case 0x8D09F56Eu:
        case 0xA0CFEC37u:
        case 0x017CDC23u:
        case 0x017CDC24u:
        case 0x017CDC25u:
        case 0x017CDC26u:
        case 0x017CDC27u:
            return true;
        default:
            return false;
        }
    };
    const auto is_appearance_link = [&](uint32_t hash) {
        if (is_model_field(hash)) return true;
        switch (hash) {
        case 0xA7B6EF56u:
        case 0x21D312CAu:
        case 0x0D4ADA1Au:
        case 0x7CFF5EE2u:
        case 0x29CF50D1u:
        case 0xCE642A15u:
        case 0x77679B84u:
        case 0x3C06A4E4u:
        case 0x31FF8FCFu:
        case 0x515A75DAu:
            return true;
        default:
            return false;
        }
    };
    std::vector<size_t> stack{composite_record};
    std::unordered_set<size_t> visited;
    std::vector<GdbMiniView::Field> fields;
    while (!stack.empty() && visited.size() < 512) {
        size_t current = stack.back();
        stack.pop_back();
        if (!visited.insert(current).second) continue;
        std::unordered_set<uint32_t> resolved_fields;
        std::unordered_set<size_t> inherited_records;
        for (int depth = 0; depth < 64; ++depth) {
            if (!inherited_records.insert(current).second ||
                !view.local_fields(current, fields)) {
                break;
            }
            for (const GdbMiniView::Field& field : fields) {
                if (field.hash == GdbMiniView::kHashParent) continue;
                if (!resolved_fields.insert(field.hash).second) continue;
                if (is_model_field(field.hash) &&
                    (field.type == 3 || field.type == 4 ||
                     field.type == 7)) {
                    push_unique_u32(out, field.raw);
                }
                if ((field.type == 6 || field.type == 7) &&
                    is_appearance_link(field.hash) &&
                    field.raw != 0 && field.raw != 0x811C9DC5u) {
                    size_t linked = 0;
                    if (view.lookup(field.raw, linked)) {
                        stack.push_back(linked);
                    }
                }
            }

            uint32_t parent_hash = 0;
            size_t parent_record = 0;
            if (!view.find_local(current, GdbMiniView::kHashParent, 6,
                                 parent_hash) ||
                !view.lookup(parent_hash, parent_record)) {
                break;
            }
            current = parent_record;
        }
    }
}
