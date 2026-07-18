std::unordered_map<uint32_t, std::string> LoadEmbeddedDict(
    const std::vector<uint8_t>& bytes)
{
    std::unordered_map<uint32_t, std::string> out;
    GdbView view(bytes);
    if (!view.ok) return out;
    if (bytes.size() < 0x18) return out;
    const uint32_t name_pairs = ReadBeU32(bytes.data() + 0x10);
    const size_t meta_end =
        view.offset_base + size_t(view.count) * 2;
    const size_t name_base = (meta_end + 3) & ~size_t(3);
    const size_t dict_base = name_base + size_t(name_pairs) * 8;
    if (dict_base + 12 > bytes.size()) return out;
    if (ReadBeU32(bytes.data() + dict_base) != 0x00010000u) return out;
    const uint32_t data_bytes = ReadBeU32(bytes.data() + dict_base + 4);
    const uint32_t str_count = ReadBeU32(bytes.data() + dict_base + 8);
    const size_t data_start = dict_base + 12;
    if (data_start + data_bytes > bytes.size()) return out;
    size_t off = data_start;
    const size_t data_end = data_start + data_bytes;
    out.reserve(str_count);
    for (uint32_t i = 0; i < str_count && off + 5 <= data_end; ++i) {
        const uint32_t h = ReadBeU32(bytes.data() + off);
        off += 4;
        size_t term = off;
        while (term < data_end && bytes[term] != 0) ++term;
        out.emplace(h, std::string(bytes.begin() + off,
                                   bytes.begin() + term));
        off = term + 1;
    }
    return out;
}
