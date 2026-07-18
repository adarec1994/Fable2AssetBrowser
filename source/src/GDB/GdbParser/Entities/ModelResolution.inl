std::vector<uint32_t> ModelsForEntityMulti(
    const MultiGdbRecordIndex& index, uint32_t entity_hash)
{
    std::vector<uint32_t> out_hashes;
    MultiGdbCursor ent;
    if (!MultiLookup(index, entity_hash, ent)) return out_hashes;

    constexpr uint32_t kMorphComponent = 0x0D4ADA1Au;
    constexpr uint32_t kCompositeModelRecord = 0x7CFF5EE2u;
    constexpr uint32_t kModel = 0x90347E14u;
    constexpr uint32_t kLeftEyeModel = 0x139ADC04u;
    constexpr uint32_t kRightEyeModel = 0x926B168Bu;
    constexpr uint32_t kEyeModelR = 0x8D09F54Fu;
    constexpr uint32_t kEyeModelL = 0x8D09F551u;
    constexpr uint32_t kEyeModels = 0x8D09F56Eu;
    constexpr uint32_t kEyeCaseModelL = 0x6503BD45u;
    constexpr uint32_t kEyeCaseModelR = 0x6503BD5Bu;
    constexpr uint32_t kLeftEyeCaseModel = 0xCF68CC70u;
    constexpr uint32_t kRightEyeCaseModel = 0xFB479147u;
    constexpr uint32_t kHeadAccessories = 0xA0CFEC37u;
    constexpr uint32_t kLegsAccessories = 0x3D7B3380u;
    constexpr uint32_t kTorsoAccessories = 0xF5B074D4u;
    constexpr uint32_t kRetextureableModel = 0xC483A92Au;
    constexpr uint32_t kCharacterFolder = 0x1022D6D2u;
    constexpr uint32_t kCharacter = 0xBB214642u;
    constexpr uint32_t kPartFields[] = {
        0x05294B89u,
        0x547DDA3Eu,
        0xD622E5ADu,
        kLeftEyeModel,
        kRightEyeModel,
        kEyeModelR,
        kEyeModelL,
        kEyeModels,
        kEyeCaseModelL,
        kEyeCaseModelR,
        kLeftEyeCaseModel,
        kRightEyeCaseModel,
        kHeadAccessories,
        0x017CDC23u,
        0x017CDC24u,
        0x017CDC25u,
        0x017CDC26u,
        0x017CDC27u,
    };
    constexpr uint32_t kAppearanceFields[] = {
        kHashGraphicAppearanceComponent,
        kHashGraphicAppearanceAnimatedMeshComponent,
        kHashStaticMeshComponent,
        kHashStaticMultipleMeshComponent,
        kHashStaticMultipleModelList,
        kMorphComponent,
        kCompositeModelRecord,
        0x3C06A4E4u,
        0x31FF8FCFu,
        0x515A75DAu,
    };

    auto is_model_path_field = [&](uint32_t field_hash) {
        if (field_hash == kHashModelFile ||
            field_hash == kHashModelFile1 ||
            field_hash == kHashModelFile2 ||
            field_hash == kHashStaticMultipleModelFile ||
            field_hash == kModel) {
            return true;
        }
        return std::find(std::begin(kPartFields), std::end(kPartFields),
                         field_hash) != std::end(kPartFields);
    };
    auto is_appearance_field = [&](uint32_t field_hash) {
        if (is_model_path_field(field_hash)) return true;
        if (field_hash == kLegsAccessories ||
            field_hash == kTorsoAccessories ||
            field_hash == kRetextureableModel ||
            field_hash == kCharacterFolder ||
            field_hash == kCharacter) {
            return true;
        }
        if (std::find(std::begin(kAppearanceFields),
                      std::end(kAppearanceFields), field_hash) !=
            std::end(kAppearanceFields)) {
            return true;
        }
        return std::find(std::begin(kPartFields), std::end(kPartFields),
                         field_hash) != std::end(kPartFields);
    };
    auto is_inherited_list_entry = [&](uint32_t field_hash) {




        return field_hash == kRetextureableModel ||
               field_hash == kCharacter ||
               field_hash == kHashStaticMultipleModelFile;
    };
    auto add_model = [&](uint32_t hash) {


        if (hash == 0 || hash == kHashNull || hash == 0x811C9DC5u) return;
        if (std::find(out_hashes.begin(), out_hashes.end(), hash) ==
            out_hashes.end()) {
            out_hashes.push_back(hash);
        }
    };
    auto read_local_fields = [](MultiGdbCursor cur,
                                const auto& visit) {
        size_t schema = 0;
        uint32_t field_count = 0;
        if (!cur.view->schema(cur.record, schema, field_count)) return;
        const size_t hashes = schema + 4;
        const size_t descs = hashes + size_t(field_count) * 4;
        for (uint32_t i = 0; i < field_count; ++i) {
            const size_t slot = cur.record + 4 + size_t(i) * 4;
            if (slot + 4 > cur.view->body_end) break;
            const uint32_t field_hash = ReadBeU32(
                cur.view->bytes.data() + hashes + size_t(i) * 4);
            const uint8_t type = uint8_t(ReadBeU32(
                cur.view->bytes.data() + descs + size_t(i) * 4) >> 24);
            const uint32_t raw =
                ReadBeU32(cur.view->bytes.data() + slot);
            visit(field_hash, type, raw);
        }
    };

    std::vector<MultiGdbCursor> appearance_roots;
    std::unordered_set<uint32_t> resolved_entity_fields;
    std::unordered_set<uint32_t> entity_parents;
    MultiGdbCursor cur = ent;
    for (int depth = 0; depth < 64; ++depth) {
        uint32_t parent_hash = 0;
        read_local_fields(cur, [&](uint32_t field_hash, uint8_t type,
                                   uint32_t raw) {
            if (field_hash == kHashParent && type == 6) {
                parent_hash = raw;
                return;
            }



            if (!resolved_entity_fields.insert(field_hash).second) return;
            if ((type == 3 || type == 4 || type == 7) &&
                is_model_path_field(field_hash)) {
                add_model(raw);
            }
            if ((type == 6 || type == 7) &&
                is_appearance_field(field_hash)) {
                MultiGdbCursor linked;
                if (MultiLookup(index, raw, linked)) {
                    appearance_roots.push_back(linked);
                }
            }
        });
        if (parent_hash == 0 || parent_hash == kHashNull) break;
        if (!entity_parents.insert(parent_hash).second) break;
        MultiGdbCursor parent;
        if (!MultiLookup(index, parent_hash, parent)) break;
        cur = parent;
    }





    std::vector<MultiGdbCursor> stack = std::move(appearance_roots);
    std::unordered_set<uint32_t> visited;
    while (!stack.empty() && visited.size() < 512) {
        MultiGdbCursor node = stack.back();
        stack.pop_back();
        uint32_t node_hash = 0;
        const auto it = std::lower_bound(node.view->record_data_offsets.begin(),
                                         node.view->record_data_offsets.end(),
                                         node.record);
        if (it != node.view->record_data_offsets.end() &&
            *it == node.record) {
            const size_t record_index = size_t(
                it - node.view->record_data_offsets.begin());
            node_hash = ReadBeU32(node.view->bytes.data() +
                                  node.view->hash_base + record_index * 4);
        }
        if (node_hash != 0 && !visited.insert(node_hash).second) continue;

        std::unordered_set<uint32_t> resolved_fields;
        std::unordered_set<uint32_t> inherited_records;
        MultiGdbCursor inherited = node;
        bool selected_character_variant = false;
        for (int depth = 0; depth < 64; ++depth) {
            uint32_t parent_hash = 0;
            std::unordered_set<uint32_t> fields_in_this_record;
            read_local_fields(inherited,
                [&](uint32_t field_hash, uint8_t type, uint32_t raw) {
                    if (field_hash == kHashParent && type == 6) {
                        parent_hash = raw;
                        return;
                    }
                    if (!is_inherited_list_entry(field_hash) &&
                        resolved_fields.find(field_hash) !=
                            resolved_fields.end()) {
                        return;
                    }




                    if (field_hash == kCharacter) {
                        if (selected_character_variant) return;
                        selected_character_variant = true;
                    }
                    if (!is_inherited_list_entry(field_hash)) {
                        fields_in_this_record.insert(field_hash);
                    }
                    if ((type == 3 || type == 4 || type == 7) &&
                        is_model_path_field(field_hash)) {
                        add_model(raw);
                    }
                    if ((type == 6 || type == 7) &&
                        is_appearance_field(field_hash) &&
                        raw != 0 && raw != kHashNull) {
                        MultiGdbCursor linked;
                        if (MultiLookup(index, raw, linked)) {
                            stack.push_back(linked);
                        }
                    }
                });
            resolved_fields.insert(fields_in_this_record.begin(),
                                   fields_in_this_record.end());
            if (parent_hash == 0 || parent_hash == kHashNull ||
                !inherited_records.insert(parent_hash).second) {
                break;
            }
            MultiGdbCursor parent;
            if (!MultiLookup(index, parent_hash, parent)) break;
            inherited = parent;
        }
    }
    return out_hashes;
}
