#include "GdbEdit.h"

#include <algorithm>
#include <cstring>

namespace GdbEdit {

namespace {

constexpr size_t kHeaderSize = 0x18;
constexpr uint32_t kDictVersion = 0x00010000u;

inline uint32_t rbe32(const std::vector<uint8_t>& b, size_t off)
{
    return (uint32_t(b[off]) << 24) | (uint32_t(b[off + 1]) << 16) |
           (uint32_t(b[off + 2]) << 8) | uint32_t(b[off + 3]);
}

inline uint16_t rbe16(const std::vector<uint8_t>& b, size_t off)
{
    return uint16_t((uint16_t(b[off]) << 8) | uint16_t(b[off + 1]));
}

inline void wbe32(std::vector<uint8_t>& b, uint32_t v)
{
    b.push_back(uint8_t(v >> 24));
    b.push_back(uint8_t(v >> 16));
    b.push_back(uint8_t(v >> 8));
    b.push_back(uint8_t(v));
}

inline void wbe16(std::vector<uint8_t>& b, uint16_t v)
{
    b.push_back(uint8_t(v >> 8));
    b.push_back(uint8_t(v));
}

uint32_t fnv1(const std::string& s)
{
    uint32_t h = 0x811C9DC5u;
    for (unsigned char c : s) {
        h *= 0x01000193u;
        h ^= uint32_t(c);
    }
    return h;
}

uint32_t schema_field_count(const std::vector<uint8_t>& b, size_t off)
{
    const uint32_t header = rbe32(b, off);
    uint32_t n = header >> 8;
    if (n > 256) {
        n = (uint32_t(b[off]) | (uint32_t(b[off + 1]) << 8)) +
            uint32_t(b[off + 2]);
    }
    return n;
}

}

bool GdbFile::Parse(const std::vector<uint8_t>& bytes, std::string& err)
{
    records_.clear();
    schemas_.clear();
    schema_by_rel_.clear();
    record_index_by_hash_.clear();
    name_map_.clear();
    dict_entries_.clear();
    dict_by_hash_.clear();

    if (bytes.size() < kHeaderSize ||
        bytes[0] != 'G' || bytes[1] != 'D' || bytes[2] != 'B' || bytes[3]) {
        err = "not a GDB file";
        return false;
    }
    const uint32_t count = rbe32(bytes, 0x04);
    const uint32_t size_a = rbe32(bytes, 0x08);
    const uint32_t size_b = rbe32(bytes, 0x0C);
    const uint32_t name_pairs = rbe32(bytes, 0x10);
    header_unknown14_ = rbe32(bytes, 0x14);
    schema_blob_size_ = size_b;

    const size_t schema_base = kHeaderSize + size_a;
    const size_t hash_base = schema_base + size_b;
    const size_t meta_base = hash_base + size_t(count) * 4;
    const size_t name_base_raw = meta_base + size_t(count) * 2;
    const size_t name_base = (name_base_raw + 3) & ~size_t(3);
    if (hash_base > bytes.size() || meta_base > bytes.size() ||
        name_base + size_t(name_pairs) * 8 > bytes.size()) {
        err = "GDB truncated";
        return false;
    }

    {
        size_t off = schema_base;
        while (off + 4 <= hash_base) {
            const uint32_t n = schema_field_count(bytes, off);
            if (n > 1024 || off + 4 + size_t(n) * 8 > hash_base) {
                err = "bad schema walk";
                return false;
            }
            Schema s;
            s.rel = uint32_t(off - schema_base);
            s.header_low = rbe32(bytes, off) & 0xFFu;
            s.fields.reserve(n);
            const size_t hashes = off + 4;
            const size_t descs = hashes + size_t(n) * 4;
            for (uint32_t i = 0; i < n; ++i) {
                SchemaField f;
                f.hash = rbe32(bytes, hashes + size_t(i) * 4);
                const uint32_t desc = rbe32(bytes, descs + size_t(i) * 4);
                f.type = uint8_t(desc >> 24);
                f.decl = desc & 0xFFFFFFu;
                s.fields.push_back(f);
            }
            schema_by_rel_.emplace(s.rel, schemas_.size());
            schemas_.push_back(std::move(s));
            off += 4 + size_t(n) * 8;
        }
        if (off != hash_base) {
            err = "schema blob size mismatch";
            return false;
        }
    }

    {
        size_t cur = kHeaderSize;
        records_.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            if (cur + 4 > schema_base) {
                err = "record walk past body";
                return false;
            }
            Record r;
            r.schema_rel = rbe32(bytes, cur);
            auto sit = schema_by_rel_.find(r.schema_rel);
            if (sit == schema_by_rel_.end()) {
                err = "record references unknown schema";
                return false;
            }
            const uint32_t n = uint32_t(schemas_[sit->second].fields.size());
            if (cur + 4 + size_t(n) * 4 > schema_base) {
                err = "record slots past body";
                return false;
            }
            r.hash = rbe32(bytes, hash_base + size_t(i) * 4);
            r.meta = rbe16(bytes, meta_base + size_t(i) * 2);
            r.slots.reserve(n);
            for (uint32_t k = 0; k < n; ++k) {
                r.slots.push_back(rbe32(bytes, cur + 4 + size_t(k) * 4));
            }
            record_index_by_hash_.emplace(r.hash, int(records_.size()));
            records_.push_back(std::move(r));
            cur += 4 + size_t(n) * 4;
        }
        if (cur != schema_base) {
            err = "body size mismatch";
            return false;
        }
    }

    name_map_.reserve(name_pairs);
    for (uint32_t i = 0; i < name_pairs; ++i) {
        const size_t off = name_base + size_t(i) * 8;
        name_map_.emplace_back(rbe32(bytes, off), rbe32(bytes, off + 4));
    }

    {
        const size_t dict_base = name_base + size_t(name_pairs) * 8;
        if (dict_base + 12 <= bytes.size()) {
            const uint32_t ver = rbe32(bytes, dict_base);
            const uint32_t data_bytes = rbe32(bytes, dict_base + 4);
            const uint32_t str_count = rbe32(bytes, dict_base + 8);
            const size_t data_start = dict_base + 12;
            if (ver == kDictVersion &&
                data_start + data_bytes + size_t(str_count) * 4 <=
                    bytes.size()) {
                size_t off = data_start;
                const size_t data_end = data_start + data_bytes;
                dict_entries_.reserve(str_count);
                for (uint32_t i = 0; i < str_count && off + 5 <= data_end;
                     ++i) {
                    const uint32_t h = rbe32(bytes, off);
                    off += 4;
                    const auto term = std::find(bytes.begin() + off,
                                                bytes.begin() + data_end,
                                                uint8_t(0));
                    std::string s(bytes.begin() + off, term);
                    off = size_t(term - bytes.begin()) + 1;
                    dict_entries_.emplace_back(h, s);
                    dict_by_hash_.emplace(h, std::move(s));
                }
            }
        }
    }
    return true;
}

std::vector<uint8_t> GdbFile::Serialize() const
{

    std::vector<uint8_t> schema_blob;
    schema_blob.reserve(schema_blob_size_);
    for (const Schema& s : schemas_) {
        wbe32(schema_blob,
              (uint32_t(s.fields.size()) << 8) | (s.header_low & 0xFFu));
        for (const SchemaField& f : s.fields) wbe32(schema_blob, f.hash);
        for (const SchemaField& f : s.fields) {
            wbe32(schema_blob,
                  (uint32_t(f.type) << 24) | (f.decl & 0xFFFFFFu));
        }
    }

    std::vector<uint8_t> body;
    for (const Record& r : records_) {
        wbe32(body, r.schema_rel);
        for (uint32_t v : r.slots) wbe32(body, v);
    }

    std::vector<uint8_t> out;
    out.reserve(kHeaderSize + body.size() + schema_blob.size() +
                records_.size() * 6 + name_map_.size() * 8 + 4096);
    out.push_back('G');
    out.push_back('D');
    out.push_back('B');
    out.push_back(0);
    wbe32(out, uint32_t(records_.size()));
    wbe32(out, uint32_t(body.size()));
    wbe32(out, uint32_t(schema_blob.size()));
    wbe32(out, uint32_t(name_map_.size()));
    wbe32(out, header_unknown14_);
    out.insert(out.end(), body.begin(), body.end());
    out.insert(out.end(), schema_blob.begin(), schema_blob.end());
    for (const Record& r : records_) wbe32(out, r.hash);

    {
        const uint32_t count = uint32_t(records_.size());
        const uint64_t body_words = body.size() / 4;
        const uint64_t scale =
            count ? (body_words << 10) / count : 0;
        uint64_t off_words = 0;
        uint32_t i = 0;
        for (const Record& r : records_) {
            const int64_t residual =
                int64_t(off_words) - int64_t((scale * i) >> 10);
            wbe16(out, uint16_t(int16_t(residual)));
            off_words += 1 + r.slots.size();
            ++i;
        }
    }
    while (out.size() & 3) out.push_back(0);
    for (const auto& [k, v] : name_map_) {
        wbe32(out, k);
        wbe32(out, v);
    }

    std::vector<uint8_t> dict_data;
    std::vector<uint32_t> entry_offsets;
    entry_offsets.reserve(dict_entries_.size());
    for (const auto& [h, s] : dict_entries_) {
        entry_offsets.push_back(uint32_t(dict_data.size()));
        wbe32(dict_data, h);
        dict_data.insert(dict_data.end(), s.begin(), s.end());
        dict_data.push_back(0);
    }

    std::vector<int32_t> table(0x10000, -1);
    for (size_t i = 0; i < dict_entries_.size(); ++i) {
        uint32_t slot = dict_entries_[i].first & 0xFFFFu;
        while (table[slot] >= 0) slot = (slot + 1) & 0xFFFFu;
        table[slot] = int32_t(i);
    }
    std::vector<size_t> order;
    order.reserve(dict_entries_.size());
    for (uint32_t slot = 0; slot < 0x10000; ++slot) {
        if (table[slot] >= 0) order.push_back(size_t(table[slot]));
    }
    wbe32(out, kDictVersion);
    wbe32(out, uint32_t(dict_data.size()));
    wbe32(out, uint32_t(dict_entries_.size()));
    out.insert(out.end(), dict_data.begin(), dict_data.end());
    for (size_t i : order) wbe32(out, entry_offsets[i]);
    return out;
}

int GdbFile::FindRecord(uint32_t hash) const
{
    auto it = record_index_by_hash_.find(hash);
    return it == record_index_by_hash_.end() ? -1 : it->second;
}

const Record* GdbFile::RecordByHash(uint32_t hash) const
{
    const int i = FindRecord(hash);
    return i < 0 ? nullptr : &records_[size_t(i)];
}

const GdbFile::Schema* GdbFile::SchemaByRel(uint32_t rel) const
{
    auto it = schema_by_rel_.find(rel);
    return it == schema_by_rel_.end() ? nullptr : &schemas_[it->second];
}

bool GdbFile::Fields(int record_index, std::vector<Field>& out) const
{
    out.clear();
    if (record_index < 0 || size_t(record_index) >= records_.size()) {
        return false;
    }
    const Record& r = records_[size_t(record_index)];
    const Schema* s = SchemaByRel(r.schema_rel);
    if (!s || s->fields.size() != r.slots.size()) return false;
    out.reserve(s->fields.size());
    for (size_t i = 0; i < s->fields.size(); ++i) {
        Field f;
        f.hash = s->fields[i].hash;
        f.type = s->fields[i].type;
        f.value = r.slots[i];
        out.push_back(f);
    }
    return true;
}

bool GdbFile::FindLocalField(uint32_t rec_hash, uint32_t field_hash,
                             Field& out) const
{
    std::vector<Field> fields;
    if (!Fields(FindRecord(rec_hash), fields)) return false;
    for (const Field& f : fields) {
        if (f.hash == field_hash) {
            out = f;
            return true;
        }
    }
    return false;
}

bool GdbFile::SetFieldValue(uint32_t rec_hash, uint32_t field_hash,
                            uint32_t value)
{
    const int idx = FindRecord(rec_hash);
    if (idx < 0) return false;
    Record& r = records_[size_t(idx)];
    const Schema* s = SchemaByRel(r.schema_rel);
    if (!s) return false;
    for (size_t i = 0; i < s->fields.size() && i < r.slots.size(); ++i) {
        if (s->fields[i].hash == field_hash) {
            r.slots[i] = value;
            return true;
        }
    }
    return false;
}

uint32_t GdbFile::EnsureSchema(const std::vector<SchemaField>& fields,
                               uint32_t header_low)
{
    for (const Schema& s : schemas_) {
        if (s.fields.size() != fields.size()) continue;
        bool same = true;
        for (size_t i = 0; i < fields.size(); ++i) {
            if (s.fields[i].hash != fields[i].hash ||
                s.fields[i].type != fields[i].type) {
                same = false;
                break;
            }
        }
        if (same) return s.rel;
    }
    Schema ns;
    ns.header_low = header_low;
    ns.fields = fields;
    ns.rel = uint32_t(schema_blob_size());
    schema_by_rel_.emplace(ns.rel, schemas_.size());
    schemas_.push_back(std::move(ns));
    return schemas_.back().rel;
}

size_t GdbFile::schema_blob_size() const
{
    size_t total = 0;
    for (const Schema& s : schemas_) total += 4 + s.fields.size() * 8;
    return total;
}

bool GdbFile::AddField(uint32_t rec_hash, uint32_t field_hash, uint8_t type,
                       uint32_t value)
{
    const int idx = FindRecord(rec_hash);
    if (idx < 0) return false;
    Record& r = records_[size_t(idx)];
    const Schema* s = SchemaByRel(r.schema_rel);
    if (!s || s->fields.size() != r.slots.size()) return false;
    std::vector<SchemaField> fields = s->fields;
    for (const SchemaField& f : fields) {
        if (f.hash == field_hash) return false;
    }

    size_t pos = 0;
    while (pos < fields.size() && fields[pos].hash < field_hash) ++pos;
    SchemaField nf;
    nf.hash = field_hash;
    nf.type = type;
    nf.decl = uint32_t(fields.size());
    fields.insert(fields.begin() + pos, nf);
    r.schema_rel = EnsureSchema(fields, s->header_low);
    r.slots.insert(r.slots.begin() + pos, value);
    return true;
}

bool GdbFile::AddRecord(uint32_t new_hash, std::vector<Field> fields,
                        uint16_t meta)
{
    if (record_index_by_hash_.count(new_hash)) return false;
    if (!records_.empty() && new_hash <= records_.back().hash) return false;
    std::sort(fields.begin(), fields.end(),
              [](const Field& a, const Field& b) { return a.hash < b.hash; });
    std::vector<SchemaField> sfields;
    sfields.reserve(fields.size());
    for (size_t i = 0; i < fields.size(); ++i) {
        SchemaField sf;
        sf.hash = fields[i].hash;
        sf.type = fields[i].type;
        sf.decl = uint32_t(i);
        sfields.push_back(sf);
    }
    Record r;
    r.hash = new_hash;
    r.schema_rel = EnsureSchema(sfields, 0);

    r.meta = meta;
    r.slots.reserve(fields.size());
    for (const Field& f : fields) r.slots.push_back(f.value);
    record_index_by_hash_.emplace(new_hash, int(records_.size()));
    records_.push_back(std::move(r));
    return true;
}

uint32_t GdbFile::AllocRecordHash()
{
    uint32_t h = records_.empty() ? 0x80000000u : records_.back().hash;

    do {
        ++h;
    } while (h == 0x811C9DC5u || h == 0);
    return h;
}

void GdbFile::AddNameMapping(const std::string& name, uint32_t rec_hash)
{
    const uint32_t key = fnv1(name);
    auto it = std::lower_bound(
        name_map_.begin(), name_map_.end(), key,
        [](const std::pair<uint32_t, uint32_t>& p, uint32_t k) {
            return p.first < k;
        });
    if (it != name_map_.end() && it->first == key) {
        it->second = rec_hash;
        return;
    }
    name_map_.insert(it, {key, rec_hash});
}

void GdbFile::AddDictString(uint32_t hash, const std::string& text)
{
    if (dict_by_hash_.count(hash)) return;
    dict_entries_.emplace_back(hash, text);
    dict_by_hash_.emplace(hash, text);
}

uint16_t GdbFile::MetaOf(uint32_t rec_hash) const
{
    const int i = FindRecord(rec_hash);
    return i < 0 ? 0 : records_[size_t(i)].meta;
}

}
