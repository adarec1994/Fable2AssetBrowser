struct GdbMiniView {
    static constexpr size_t kHeaderSize = 0x18;
    static constexpr uint32_t kHashParent = 0x5F6317D5u;

    struct Field {
        uint32_t hash = 0;
        uint8_t type = 0;
        uint32_t raw = 0;
    };

    const std::vector<uint8_t>& bytes;
    uint32_t count = 0;
    uint32_t size_a = 0;
    uint32_t size_b = 0;
    uint32_t tail_count = 0;
    uint32_t ext_count = 0;
    size_t schema_base = 0;
    size_t hash_base = 0;
    size_t body_end = 0;
    bool ok = false;

    std::vector<size_t> record_offsets;
    std::unordered_map<uint32_t, std::string> strings;

    explicit GdbMiniView(const std::vector<uint8_t>& b) : bytes(b) {
        if (bytes.size() < kHeaderSize ||
            bytes[0] != 'G' || bytes[1] != 'D' ||
            bytes[2] != 'B' || bytes[3] != 0) {
            return;
        }

        count = be_u32_at(bytes, 0x04);
        size_a = be_u32_at(bytes, 0x08);
        size_b = be_u32_at(bytes, 0x0C);
        tail_count = be_u32_at(bytes, 0x10);
        ext_count = be_u32_at(bytes, 0x14);
        if (count == 0) return;

        schema_base = kHeaderSize + size_t(size_a);
        hash_base = schema_base + size_t(size_b);
        body_end = schema_base;
        const size_t offset_base = hash_base + size_t(count) * 4;
        if (body_end > bytes.size() ||
            offset_base + size_t(count) * 2 > bytes.size()) {
            return;
        }

        if (!build_record_offsets()) return;
        parse_string_table();
        ok = true;
    }

    bool schema_at(size_t record, size_t& schema_off,
                   uint32_t& field_count) const {
        if (record + 4 > body_end) return false;
        schema_off = schema_base + size_t(be_u32_at(bytes, record));
        if (schema_off + 4 > hash_base) return false;
        uint32_t header = be_u32_at(bytes, schema_off);
        field_count = header >> 8;
        if (field_count > 256) {
            const uint8_t* p = bytes.data() + schema_off;
            field_count = (uint32_t(p[0]) | (uint32_t(p[1]) << 8)) +
                          uint32_t(p[2]);
            if (field_count > 1024) return false;
        }
        return schema_off + 4 + size_t(field_count) * 8 <= hash_base;
    }

    bool build_record_offsets() {
        record_offsets.clear();
        record_offsets.reserve(count);
        size_t cur = kHeaderSize;
        for (uint32_t i = 0; i < count; ++i) {
            if (cur + 4 > body_end) return false;
            record_offsets.push_back(cur);
            size_t schema_off = 0;
            uint32_t field_count = 0;
            if (!schema_at(cur, schema_off, field_count)) return false;
            const size_t entry_size = 4 + size_t(field_count) * 4;
            if (cur + entry_size > body_end) return false;
            cur += entry_size;
        }
        return true;
    }

    bool lookup(uint32_t hash, size_t& record) const {
        if (!ok) return false;
        size_t lo = 0;
        size_t hi = count;
        while (lo < hi) {
            const size_t mid = lo + (hi - lo) / 2;
            const uint32_t v = be_u32_at(bytes, hash_base + mid * 4);
            if (v < hash) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        if (lo >= count) return false;
        if (be_u32_at(bytes, hash_base + lo * 4) != hash) return false;
        if (lo >= record_offsets.size()) return false;
        record = record_offsets[lo];
        return record + 4 <= body_end;
    }

    uint32_t record_hash(size_t index) const {
        if (index >= count) return 0;
        return be_u32_at(bytes, hash_base + index * 4);
    }

    uint32_t hash_for_record(size_t record) const {
        auto it = std::lower_bound(record_offsets.begin(),
                                   record_offsets.end(), record);
        if (it == record_offsets.end() || *it != record) return 0;
        return record_hash(size_t(it - record_offsets.begin()));
    }

    bool find_local(size_t record, uint32_t field_hash,
                    uint8_t expected_type, uint32_t& raw) const {
        size_t schema_off = 0;
        uint32_t field_count = 0;
        if (!schema_at(record, schema_off, field_count)) return false;
        const size_t hashes = schema_off + 4;
        const size_t descs = hashes + size_t(field_count) * 4;
        for (uint32_t i = 0; i < field_count; ++i) {
            if (be_u32_at(bytes, hashes + size_t(i) * 4) != field_hash) {
                continue;
            }
            const uint8_t type =
                uint8_t(be_u32_at(bytes, descs + size_t(i) * 4) >> 24);
            if (type != expected_type) continue;
            const size_t slot = record + 4 + size_t(i) * 4;
            if (slot + 4 > body_end) return false;
            raw = be_u32_at(bytes, slot);
            return true;
        }
        return false;
    }

    bool local_fields(size_t record, std::vector<Field>& out) const {
        out.clear();
        size_t schema_off = 0;
        uint32_t field_count = 0;
        if (!schema_at(record, schema_off, field_count)) return false;
        const size_t hashes = schema_off + 4;
        const size_t descs = hashes + size_t(field_count) * 4;
        out.reserve(field_count);
        for (uint32_t i = 0; i < field_count; ++i) {
            const size_t slot = record + 4 + size_t(i) * 4;
            if (slot + 4 > body_end) return false;
            Field f;
            f.hash = be_u32_at(bytes, hashes + size_t(i) * 4);
            f.type = uint8_t(be_u32_at(bytes, descs + size_t(i) * 4) >> 24);
            f.raw = be_u32_at(bytes, slot);
            out.push_back(f);
        }
        return true;
    }

    bool find_field(size_t record, uint32_t field_hash,
                    uint8_t expected_type, uint32_t& raw,
                    uint32_t* owner_hash = nullptr) const {
        std::unordered_set<size_t> visited;
        size_t cur = record;
        for (int depth = 0; depth < 64; ++depth) {
            if (!visited.insert(cur).second) return false;
            if (find_local(cur, field_hash, expected_type, raw)) {
                if (owner_hash) *owner_hash = hash_for_record(cur);
                return true;
            }

            uint32_t parent_hash = 0;
            if (!find_local(cur, kHashParent, 6, parent_hash) ||
                parent_hash == 0) {
                return false;
            }

            size_t parent_record = 0;
            if (!lookup(parent_hash, parent_record) ||
                parent_record == cur) {
                return false;
            }
            cur = parent_record;
        }
        return false;
    }

    void parse_string_table() {
        const size_t body_size =
            2 * (4 * (size_t(ext_count) + size_t(tail_count)) +
                 3 * size_t(count) + (size_t(count) & 1u)) +
            size_t(size_b) + size_t(size_a);
        const size_t table = kHeaderSize + body_size;
        if (table + 12 > bytes.size()) return;

        const uint32_t blob_size = be_u32_at(bytes, table + 4);
        const uint32_t string_count = be_u32_at(bytes, table + 8);
        const size_t blob = table + 12;
        if (string_count > 100000 ||
            blob + size_t(blob_size) + size_t(string_count) * 4 >
                bytes.size()) {
            return;
        }

        strings.reserve(string_count);
        for (uint32_t i = 0; i < string_count; ++i) {
            const size_t off = be_u32_at(bytes, blob + size_t(blob_size) +
                                               size_t(i) * 4);
            if (off + 4 >= blob_size) continue;
            const uint32_t h = be_u32_at(bytes, blob + off);
            size_t pos = blob + off + 4;
            size_t end = pos;
            const size_t limit = blob + size_t(blob_size);
            while (end < limit && bytes[end] != 0) ++end;
            if (end >= limit) continue;
            strings.emplace(h, std::string(
                reinterpret_cast<const char*>(bytes.data() + pos),
                end - pos));
        }
    }
};
