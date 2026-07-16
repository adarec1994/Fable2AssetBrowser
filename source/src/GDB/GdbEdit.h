#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace GdbEdit {

struct Field {
    uint32_t hash = 0;
    uint8_t  type = 0;
    uint32_t value = 0;
    uint32_t decl = 0xFFFFFFFFu;
};

struct Record {
    uint32_t hash = 0;
    uint32_t schema_rel = 0;
    std::vector<uint32_t> slots;
    uint16_t meta = 0;
};

class GdbFile {
public:
    bool Parse(const std::vector<uint8_t>& bytes, std::string& err);
    std::vector<uint8_t> Serialize() const;

    int  FindRecord(uint32_t hash) const;
    const Record* RecordByHash(uint32_t hash) const;


    uint32_t ResolveNamedRecord(const std::string& name) const;

    bool Fields(int record_index, std::vector<Field>& out) const;
    bool FindLocalField(uint32_t rec_hash, uint32_t field_hash,
                        Field& out) const;

    bool SetFieldValue(uint32_t rec_hash, uint32_t field_hash,
                       uint32_t value);

    bool AddField(uint32_t rec_hash, uint32_t field_hash, uint8_t type,
                  uint32_t value, uint32_t decl = 0xFFFFFFFFu);
    bool RemoveField(uint32_t rec_hash, uint32_t field_hash);

    bool AddRecord(uint32_t new_hash, std::vector<Field> fields,
                   uint8_t schema_header_low);
    bool RemoveRecord(uint32_t rec_hash);

    uint32_t AllocRecordHash();

    void AddNameMapping(const std::string& name, uint32_t rec_hash);
    void AddDictString(uint32_t hash, const std::string& text);

    const std::unordered_map<uint32_t, std::string>& Dict() const {
        return dict_by_hash_;
    }

    size_t RecordCount() const { return records_.size(); }
    const Record& RecordAt(size_t i) const { return records_[i]; }
    uint16_t MetaOf(uint32_t rec_hash) const;
    uint32_t SchemaHeaderLow(uint32_t rec_hash) const;

private:

    struct SchemaField {
        uint32_t hash = 0;
        uint8_t  type = 0;
        uint32_t decl = 0;
    };
    struct Schema {
        std::vector<SchemaField> fields;
        uint32_t rel = 0;
        uint32_t header_low = 0;
    };

    uint32_t EnsureSchema(const std::vector<SchemaField>& fields,
                          uint32_t header_low, bool exact = true);
    const Schema* SchemaByRel(uint32_t rel) const;
    size_t schema_blob_size() const;

    std::vector<Record> records_;
    std::vector<Schema> schemas_;
    std::unordered_map<uint32_t, size_t> schema_by_rel_;
    std::unordered_map<uint32_t, int> record_index_by_hash_;
    std::vector<std::pair<uint32_t, uint32_t>> name_map_;

    std::vector<std::pair<uint32_t, std::string>> dict_entries_;
    std::unordered_map<uint32_t, std::string> dict_by_hash_;
    uint32_t header_unknown14_ = 0;
    uint32_t schema_blob_size_ = 0;
};

}
