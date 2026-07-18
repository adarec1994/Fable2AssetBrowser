bool append_additions_to_level(std::vector<uint8_t>& bytes,
                               const std::vector<Addition>& adds,
                               std::string& err) {
    static const char kMagic[] = "LevelGraphicsFile";
    const size_t magic_len = sizeof(kMagic) - 1;
    if (bytes.size() < magic_len + 8 ||
        std::memcmp(bytes.data(), kMagic, magic_len) != 0) {
        err = "level payload magic mismatch";
        return false;
    }
    uint32_t alive = 0;
    for (const auto& a : adds) {
        if (!a.removed) ++alive;
    }
    const size_t count_off = magic_len + 4;
    uint32_t entry_count =
        (uint32_t(bytes[count_off]) << 24) |
        (uint32_t(bytes[count_off + 1]) << 16) |
        (uint32_t(bytes[count_off + 2]) << 8) |
        uint32_t(bytes[count_off + 3]);
    entry_count += alive;
    bytes[count_off]     = uint8_t(entry_count >> 24);
    bytes[count_off + 1] = uint8_t(entry_count >> 16);
    bytes[count_off + 2] = uint8_t(entry_count >> 8);
    bytes[count_off + 3] = uint8_t(entry_count);

    float tmpl[20] = { 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 1, 1, 1,
                       1024.0f, 0.025f, 16.0f, 64.0f, 32.0f, 0.0f, 0.1f };
    {
        float scanned[20];
        if (find_type2_template(bytes, scanned)) {
            for (int k = 13; k < 20; ++k) tmpl[k] = scanned[k];
        }
    }

    std::vector<uint8_t> tail;
    for (size_t ai = 0; ai < adds.size(); ++ai) {
        const Addition& a = adds[ai];
        if (a.removed) continue;
        put_u32_be(tail, 2);
        const std::string mp = lower_model_path(a.model_path);
        tail.insert(tail.end(), mp.begin(), mp.end());
        tail.push_back(0);
        tail.push_back(0);
        tail.push_back(0);
        tail.push_back(0);
        put_u32_be(tail, 1);
        tail.push_back(1);
        tail.push_back(0);
        tail.push_back(0);
        put_u64_be(tail, addition_instance_hash(mp, ai));
        float vals[20];
        std::memcpy(vals, tmpl, sizeof(vals));
        vals[0] = a.pos[0];
        vals[1] = a.pos[1];
        vals[2] = a.pos[2];
        vals[3] = 0.0f;
        vals[4] = 0.0f;
        vals[5] = 1.0f;
        const float yaw = a.yaw_deg * kDegToRad;
        vals[6] = std::sin(yaw);
        vals[7] = std::cos(yaw);
        vals[8] = 0.0f;
        vals[9] = vals[10] = vals[11] = vals[12] = 1.0f;
        for (int k = 0; k < 20; ++k) put_f32_be_v(tail, vals[k]);
    }
    bytes.insert(bytes.end(), tail.begin(), tail.end());
    return true;
}

struct LevelPlacementDelete {
    uint32_t pos_file_offset = 0;
    uint8_t lev_kind = 0;
};

bool remove_level_placements(
    std::vector<uint8_t>& bytes,
    const std::vector<LevelPlacementDelete>& requested,
    size_t& removed,
    std::string& err)
{
    removed = 0;
    if (requested.empty()) return true;

    Level::EngineLevelInfo level;
    if (!Level::ParseEngineLevel(bytes, level)) {
        err = "could not parse level placements: " + level.error;
        return false;
    }

    struct Record {
        uint32_t record_off = 0;
        uint32_t count_off = 0;
        uint16_t size = 0;
    };
    std::unordered_map<uint64_t, Record> records;
    for (const auto& block : level.prop_blocks) {
        for (const auto& inst : block.instances) {
            if (inst.pos_file_offset == 0 ||
                inst.record_file_offset == 0 ||
                inst.count_file_offset == 0 || inst.record_size == 0) {
                continue;
            }
            const uint64_t key =
                (uint64_t(inst.lev_rec_kind) << 32) |
                uint64_t(inst.pos_file_offset);
            records.emplace(key, Record{inst.record_file_offset,
                                        inst.count_file_offset,
                                        inst.record_size});
        }
    }

    std::vector<Record> erase_records;
    std::unordered_set<uint64_t> seen;
    for (const auto& request : requested) {
        const uint64_t key =
            (uint64_t(request.lev_kind) << 32) |
            uint64_t(request.pos_file_offset);
        if (!seen.insert(key).second) continue;
        const auto it = records.find(key);
        if (it == records.end()) {
            char detail[96];
            std::snprintf(detail, sizeof(detail),
                          "render placement kind %u at 0x%08X not found",
                          unsigned(request.lev_kind),
                          request.pos_file_offset);
            err = detail;
            return false;
        }
        erase_records.push_back(it->second);
    }

    std::sort(erase_records.begin(), erase_records.end(),
              [](const Record& a, const Record& b) {
                  return a.record_off > b.record_off;
              });
    for (const Record& record : erase_records) {
        if (uint64_t(record.record_off) + record.size > bytes.size() ||
            uint64_t(record.count_off) + 4 > bytes.size() ||
            record.count_off >= record.record_off) {
            err = "render placement record is outside the level payload";
            return false;
        }
        const uint32_t count = get_u32_be2(bytes.data() + record.count_off);
        if (count == 0) {
            err = "render placement owner already has zero instances";
            return false;
        }
        const uint32_t new_count = count - 1;
        bytes[record.count_off + 0] = uint8_t(new_count >> 24);
        bytes[record.count_off + 1] = uint8_t(new_count >> 16);
        bytes[record.count_off + 2] = uint8_t(new_count >> 8);
        bytes[record.count_off + 3] = uint8_t(new_count);
        bytes.erase(bytes.begin() + record.record_off,
                    bytes.begin() + record.record_off + record.size);
        ++removed;
    }
    return true;
}

bool patch_engine_resource_list(std::vector<uint8_t>& bytes,
                                const std::vector<uint32_t>& hashes,
                                bool& changed,
                                std::string& err) {
    changed = false;
    static const char kMagic[] = "EngineResourceList";
    const size_t magic_len = sizeof(kMagic) - 1;
    if (bytes.size() < magic_len + 9 ||
        std::memcmp(bytes.data(), kMagic, magic_len) != 0) {
        err = "engine_data magic mismatch";
        return false;
    }
    if (get_u32_be2(bytes.data() + magic_len) != 3) {
        err = "engine_data version != 3";
        return false;
    }
    const size_t cnt_off = magic_len + 5;
    uint32_t count = get_u32_be2(bytes.data() + cnt_off);
    const size_t list_off = cnt_off + 4;
    if (list_off + (uint64_t)count * 4 > bytes.size() ||
        count > (1u << 24)) {
        err = "engine_data resource list truncated";
        return false;
    }
    std::unordered_set<uint32_t> have;
    have.reserve(count * 2);
    for (uint32_t i = 0; i < count; ++i) {
        have.insert(get_u32_be2(bytes.data() + list_off + (size_t)i * 4));
    }
    std::vector<uint8_t> add;
    for (uint32_t h : hashes) {
        if (have.insert(h).second) put_u32_be(add, h);
    }
    if (add.empty()) return true;
    bytes.insert(bytes.begin() + (list_off + (size_t)count * 4),
                 add.begin(), add.end());
    count += (uint32_t)(add.size() / 4);
    bytes[cnt_off]     = uint8_t(count >> 24);
    bytes[cnt_off + 1] = uint8_t(count >> 16);
    bytes[cnt_off + 2] = uint8_t(count >> 8);
    bytes[cnt_off + 3] = uint8_t(count);
    changed = true;
    return true;
}
