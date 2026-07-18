bool LookupModelPathHash(
    const std::vector<uint8_t>& bytes,
    uint32_t record_hash,
    uint32_t& out_model_path_hash,
    uint32_t* out_parent_hash)
{
    out_model_path_hash = 0;
    std::vector<uint32_t> hashes;
    if (!LookupModelPathHashes(bytes, record_hash, hashes, out_parent_hash) ||
        hashes.empty())
    {
        return false;
    }
    out_model_path_hash = hashes.front();
    return true;
}

bool LookupModelPathHashes(
    const std::vector<uint8_t>& bytes,
    uint32_t record_hash,
    std::vector<uint32_t>& out_model_path_hashes,
    uint32_t* out_parent_hash)
{
    out_model_path_hashes.clear();
    if (out_parent_hash) *out_parent_hash = 0;
    if (record_hash == 0) return false;

    GdbView view(bytes);
    if (!view.ok) return false;

    size_t record = 0;
    if (!view.lookup(record_hash, record)) return false;

    uint32_t parent_hash = 0;
    size_t parent_slot = 0;
    if (view.findLocal(record, kHashParent, 6, parent_slot, nullptr)) {
        parent_hash = ReadBeU32(bytes.data() + parent_slot);
        if (out_parent_hash) *out_parent_hash = parent_hash;
    }

    out_model_path_hashes = CollectModelPathHashesForRecord(view, record);
    if (out_model_path_hashes.empty() && parent_hash != 0) {
        size_t parent_record = 0;
        if (view.lookup(parent_hash, parent_record)) {
            out_model_path_hashes =
                CollectModelPathHashesForRecord(view, parent_record);
        }
    }
    return !out_model_path_hashes.empty();
}
