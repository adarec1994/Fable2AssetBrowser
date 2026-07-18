namespace {

struct ADBEntry {
    std::string name;
    std::vector<uint8_t> data;
};

std::vector<ADBEntry> decompress_adb(const std::string& path) {
    std::vector<ADBEntry> result;

    std::ifstream f(path, std::ios::binary);
    if (!f) return result;

    char header[12];
    f.read(header, 12);
    if (memcmp(header, "LhCoMpReSsEd", 12) != 0) {
        return result;
    }

    uint32_t file_count;
    f.read((char*)&file_count, 4);
    file_count = (file_count >> 24) | ((file_count >> 8) & 0xFF00) | ((file_count << 8) & 0xFF0000) | (file_count << 24);

    std::string base_name = std::filesystem::path(path).stem().string();

    for (uint32_t entry_num = 0; entry_num < file_count; ++entry_num) {
        uint32_t decomp_size, comp_size;
        f.read((char*)&decomp_size, 4);
        f.read((char*)&comp_size, 4);

        decomp_size = (decomp_size >> 24) | ((decomp_size >> 8) & 0xFF00) | ((decomp_size << 8) & 0xFF0000) | (decomp_size << 24);
        comp_size = (comp_size >> 24) | ((comp_size >> 8) & 0xFF00) | ((comp_size << 8) & 0xFF0000) | (comp_size << 24);

        if (comp_size == 0 || comp_size > 100000000) break;

        std::vector<uint8_t> compressed(comp_size);
        f.read((char*)compressed.data(), comp_size);

        z_stream strm;
        memset(&strm, 0, sizeof(strm));

        if (inflateInit(&strm) != Z_OK) continue;

        std::vector<uint8_t> decompressed;
        strm.avail_in = (uInt)compressed.size();
        strm.next_in = compressed.data();

        uint8_t outbuffer[32768];
        do {
            strm.avail_out = sizeof(outbuffer);
            strm.next_out = outbuffer;

            int ret = inflate(&strm, Z_NO_FLUSH);
            if (ret != Z_OK && ret != Z_STREAM_END) {
                inflateEnd(&strm);
                break;
            }

            size_t have = sizeof(outbuffer) - strm.avail_out;
            decompressed.insert(decompressed.end(), outbuffer, outbuffer + have);

            if (ret == Z_STREAM_END) break;
        } while (strm.avail_out == 0);

        inflateEnd(&strm);

        if (!decompressed.empty()) {
            ADBEntry entry;
            if (file_count == 1) {
                entry.name = base_name + ".bin";
            } else {
                char buf[32];
                snprintf(buf, sizeof(buf), "%s_%04u.bin", base_name.c_str(), entry_num);
                entry.name = buf;
            }
            entry.data = decompressed;
            result.push_back(entry);
        }
    }

    return result;
}

}
