std::vector<RecordRow> Build010RecordRows(
    const std::vector<uint8_t>& bytes,
    const std::vector<std::pair<uint32_t, std::string>>& hash_to_name)
{
    std::vector<RecordRow> rows;
    GdbView view(bytes);
    if (!view.ok) return rows;

    std::unordered_map<uint32_t, std::string> name_by_hash;
    name_by_hash.reserve(hash_to_name.size() * 2);
    for (const auto& kv : hash_to_name) {
        name_by_hash.emplace(kv.first, kv.second);
    }

    rows.reserve(view.count);
    for (uint32_t i = 0; i < view.count; ++i) {
        const uint32_t hash =
            ReadBeU32(bytes.data() + view.hash_base + size_t(i) * 4);
        if (i >= view.record_data_offsets.size()) break;
        const size_t record = view.record_data_offsets[i];

        RecordRow row;
        row.index = i;
        row.hash = hash;
        row.name = GdbHashName(hash, name_by_hash);

        size_t parent_slot = 0;
        if (view.findLocal(record, kHashParent, 6, parent_slot, nullptr)) {
            row.parent_hash = ReadBeU32(bytes.data() + parent_slot);
        }

        row.model_path_hashes = CollectModelPathHashesForRecord(view, record);
        if (row.model_path_hashes.empty() && row.parent_hash != 0) {
            size_t parent_record = 0;
            if (view.lookup(row.parent_hash, parent_record)) {
                row.model_path_hashes =
                    CollectModelPathHashesForRecord(view, parent_record);
            }
        }
        if (!row.model_path_hashes.empty()) {
            row.model_path_hash = row.model_path_hashes.front();
        }

        TryReadInheritedHashField(view, record, kHashSkeletonFile,
                                  row.skeleton_file_hash);
        TryReadInheritedHashField(view, record, kHashRetargetSkeletonFile,
                                  row.retarget_skeleton_file_hash);
        if (row.skeleton_file_hash != 0) {
            row.skeleton_file_name =
                GdbHashName(row.skeleton_file_hash, name_by_hash);
        }
        if (row.retarget_skeleton_file_hash != 0) {
            row.retarget_skeleton_file_name =
                GdbHashName(row.retarget_skeleton_file_hash, name_by_hash);
        }

        rows.push_back(std::move(row));
    }

    return rows;
}
