std::vector<MorphTargetPair> BuildMorphTargets(
    const std::vector<const std::vector<uint8_t>*>& gdbs)
{
    constexpr uint32_t kMorphTargets = 0xA1F45CDDu;
    constexpr uint32_t kOriginalModel = 0xD97C293Bu;
    constexpr uint32_t kTargetModel = 0x1A77CE85u;
    constexpr uint32_t kMorphType = 0x3BF01BE1u;

    std::vector<std::unique_ptr<GdbView>> owned;
    std::vector<const GdbView*> views;
    for (const auto* gdb : gdbs) {
        if (!gdb || gdb->empty()) continue;
        auto view = std::make_unique<GdbView>(*gdb);
        if (!view->ok) continue;
        views.push_back(view.get());
        owned.push_back(std::move(view));
    }
    if (views.empty()) return {};

    const MultiGdbRecordIndex index = BuildMultiGdbRecordIndex(views);
    std::vector<MorphTargetPair> result;
    std::unordered_set<uint32_t> seen_lists;
    struct PairKey {
        uint32_t original;
        uint32_t target;
        uint32_t morph_type;
        bool operator==(const PairKey& other) const {
            return original == other.original && target == other.target &&
                   morph_type == other.morph_type;
        }
    };
    struct PairKeyHash {
        size_t operator()(const PairKey& key) const {
            size_t hash = key.original;
            hash ^= size_t(key.target) + 0x9E3779B9u + (hash << 6) +
                    (hash >> 2);
            hash ^= size_t(key.morph_type) + 0x9E3779B9u + (hash << 6) +
                    (hash >> 2);
            return hash;
        }
    };
    std::unordered_set<PairKey, PairKeyHash> seen_pairs;

    auto read_field = [&](const MultiGdbCursor& record, uint32_t field,
                          uint32_t& value, uint8_t& type) {
        MultiGdbCursor owner;
        return MultiFindInherited(index, record, field, 0xFF,
                                  owner, value, &type);
    };

    auto collect_list = [&](uint32_t list_hash) {
        MultiGdbCursor walk;
        if (!MultiLookup(index, list_hash, walk)) return;
        std::unordered_set<uint32_t> inherited_slots;
        std::unordered_set<uint32_t> visited_records;
        for (int depth = 0; depth < 64; ++depth) {
            size_t schema = 0;
            uint32_t field_count = 0;
            if (!walk.view->schema(walk.record, schema, field_count)) break;
            const size_t hashes = schema + 4;
            const size_t descriptors = hashes + size_t(field_count) * 4;
            std::unordered_set<uint32_t> local_slots;
            for (uint32_t field = 0; field < field_count; ++field) {
                const uint32_t field_hash = ReadBeU32(
                    walk.view->bytes.data() + hashes + size_t(field) * 4);
                if (field_hash == kHashParent ||
                    inherited_slots.find(field_hash) != inherited_slots.end()) {
                    continue;
                }
                local_slots.insert(field_hash);
                const uint32_t descriptor = ReadBeU32(
                    walk.view->bytes.data() + descriptors + size_t(field) * 4);
                if (uint8_t(descriptor >> 24) != 6) continue;
                const size_t value_slot = walk.record + 4 + size_t(field) * 4;
                if (value_slot + 4 > walk.view->body_end) continue;
                const uint32_t entry_hash = ReadBeU32(
                    walk.view->bytes.data() + value_slot);
                MultiGdbCursor entry;
                if (!MultiLookup(index, entry_hash, entry)) continue;

                uint32_t original = 0;
                uint32_t target = 0;
                uint32_t morph_type = 0;
                uint8_t original_type = 0;
                uint8_t target_type = 0;
                uint8_t morph_field_type = 0;
                if (!read_field(entry, kOriginalModel, original,
                                original_type) ||
                    !read_field(entry, kTargetModel, target, target_type) ||
                    !read_field(entry, kMorphType, morph_type,
                                morph_field_type) ||
                    (original_type != 4 && original_type != 7) ||
                    (target_type != 4 && target_type != 7) ||
                    (morph_field_type != 1 && morph_field_type != 5) ||
                    original == 0 || original == kHashNull || target == 0 ||
                    target == kHashNull) {
                    continue;
                }
                if (seen_pairs.insert({original, target, morph_type}).second) {
                    result.push_back({original, target, morph_type});
                }
            }
            inherited_slots.insert(local_slots.begin(), local_slots.end());

            size_t parent_slot = 0;
            if (!walk.view->findLocal(walk.record, kHashParent, 6,
                                      parent_slot, nullptr)) break;
            const uint32_t parent = ReadBeU32(
                walk.view->bytes.data() + parent_slot);
            if (parent == 0 || parent == kHashNull ||
                !visited_records.insert(parent).second) break;
            MultiGdbCursor next;
            if (!MultiLookup(index, parent, next)) break;
            walk = next;
        }
    };

    for (const GdbView* view : views) {
        const size_t count = std::min<size_t>(
            view->count, view->record_data_offsets.size());
        for (size_t record_index = 0; record_index < count; ++record_index) {
            MultiGdbCursor record{view, view->record_data_offsets[record_index]};
            MultiGdbCursor owner;
            uint32_t list_hash = 0;
            if (MultiFindInherited(index, record, kMorphTargets, 6,
                                   owner, list_hash) &&
                list_hash != 0 && list_hash != kHashNull &&
                seen_lists.insert(list_hash).second) {
                collect_list(list_hash);
            }
        }
    }
    return result;
}
