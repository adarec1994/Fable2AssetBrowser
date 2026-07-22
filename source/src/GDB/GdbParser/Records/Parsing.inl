GdbInfo Parse(const std::vector<uint8_t>& bytes) {
    return ParseWithSaveMap(bytes, {});
}

bool LookupPlacement(
    const std::vector<uint8_t>& bytes,
    uint32_t record_hash,
    const std::string& entity_name,
    Placement& out_placement,
    bool lenient)
{
    if (record_hash == 0) return false;

    GdbView view(bytes);
    if (!view.ok) return false;

    size_t record = 0;
    if (!view.lookup(record_hash, record)) return false;

    float pos_x = 0.0f, pos_y = 0.0f, pos_z = 0.0f;
    float rot_x = 0.0f, rot_y = 0.0f, rot_z = 0.0f;
    bool has_rotation = false;
    size_t pos_slots[3] = {0, 0, 0};
    size_t rot_slots[3] = {0, 0, 0};
    bool have_pos = TryComponentTransformRecord(view, record,
                                                pos_x, pos_y, pos_z,
                                                rot_x, rot_y, rot_z,
                                                has_rotation, pos_slots,
                                                rot_slots, lenient);
    if (!have_pos) {
        have_pos = TryTransformRecord(view, record,
                                      pos_x, pos_y, pos_z,
                                      rot_x, rot_y, rot_z,
                                      has_rotation, pos_slots, rot_slots,
                                      lenient);
    }
    if (!have_pos) {
        have_pos = TryIndexedInlineTransform(view, record,
                                             pos_x, pos_y, pos_z,
                                             rot_x, rot_y, rot_z,
                                             has_rotation, pos_slots);
    }
    if (!have_pos) return false;

    Placement pl;
    pl.x = pos_x;
    pl.y = pos_y;
    pl.z = pos_z;
    pl.pos_value_off[0] = (uint32_t)pos_slots[0];
    pl.pos_value_off[1] = (uint32_t)pos_slots[1];
    pl.pos_value_off[2] = (uint32_t)pos_slots[2];
    pl.rot_value_off[0] = (uint32_t)rot_slots[0];
    pl.rot_value_off[1] = (uint32_t)rot_slots[1];
    pl.rot_value_off[2] = (uint32_t)rot_slots[2];
    pl.rot_x = rot_x;
    pl.rot_y = rot_y;
    pl.rot_z = rot_z;
    pl.has_rotation = has_rotation;
    pl.yaw = has_rotation && std::isfinite(rot_x) ? rot_x : 0.0f;
    pl.scale = 1.0f;
    pl.marker = kVarMarker;
    pl.hash_a = record_hash;
    pl.parent_hash = 0;
    pl.model_path_hash = 0;
    pl.skeleton_file_hash = 0;
    pl.retarget_skeleton_file_hash = 0;
    pl.model_path_hashes.clear();
    pl.indexed_record = true;
    pl.transform_from_indexed_record = true;
    pl.entity_name = entity_name;

    size_t parent_slot = 0;
    if (view.findLocal(record, kHashParent, 6, parent_slot, nullptr)) {
        pl.parent_hash = ReadBeU32(bytes.data() + parent_slot);
    }

    pl.model_path_hashes = CollectModelPathHashesForRecord(view, record);
    if (pl.model_path_hashes.empty() && pl.parent_hash != 0) {
        size_t parent_record = 0;
        if (view.lookup(pl.parent_hash, parent_record)) {
            pl.model_path_hashes =
                CollectModelPathHashesForRecord(view, parent_record);
        }
    }
    if (!pl.model_path_hashes.empty()) {
        pl.model_path_hash = pl.model_path_hashes.front();
    }

    TryReadInheritedHashField(view, record, kHashSkeletonFile,
                              pl.skeleton_file_hash);
    TryReadInheritedHashField(view, record, kHashRetargetSkeletonFile,
                              pl.retarget_skeleton_file_hash);

    out_placement = std::move(pl);
    return true;
}

GdbInfo ParseWithSaveMap(
    const std::vector<uint8_t>& bytes,
    const std::vector<std::pair<uint32_t, std::string>>& hash_to_name)
{
    GdbInfo out;
    if (bytes.size() < kHeaderSize) return out;

    if (bytes[0] != 'G' || bytes[1] != 'D' || bytes[2] != 'B' || bytes[3] != 0) {
        return out;
    }

    std::unordered_map<uint32_t, std::string> name_by_hash;
    name_by_hash.reserve(hash_to_name.size() * 2);
    for (const auto& kv : hash_to_name) {
        name_by_hash.emplace(kv.first, kv.second);
    }

    GdbView view(bytes);
    if (!view.ok) return out;

    std::unordered_set<uint32_t> emitted;
    emitted.reserve(hash_to_name.empty()
                        ? size_t(view.count)
                        : hash_to_name.size() * 2);

    auto emit_for_hash = [&](uint32_t inst_hash,
                             const std::string& entity_name) {
        if (inst_hash == 0 || emitted.find(inst_hash) != emitted.end()) {
            return;
        }

        Placement pl;
        if (!LookupPlacement(bytes, inst_hash, entity_name, pl,
                             !entity_name.empty())) {
            return;
        }

        if (pl.model_path_hashes.size() > 1) {
            const std::vector<uint32_t> hashes = pl.model_path_hashes;
            for (uint32_t h : hashes) {
                Placement part = pl;
                part.model_path_hash = h;
                part.model_path_hashes.clear();
                part.model_path_hashes.push_back(h);
                out.placements.push_back(std::move(part));
            }
        } else {
            out.placements.push_back(std::move(pl));
        }
        emitted.insert(inst_hash);
    };

    if (!hash_to_name.empty()) {
        for (const auto& kv : hash_to_name) {
            emit_for_hash(kv.first, kv.second);
        }
    } else {
        for (uint32_t i = 0; i < view.count; ++i) {
            const uint32_t h =
                ReadBeU32(bytes.data() + view.hash_base + size_t(i) * 4);
            emit_for_hash(h, {});
        }
    }

    return out;
}
