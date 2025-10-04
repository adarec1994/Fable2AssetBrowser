#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <optional>
#include <filesystem>
#include <fstream>
#include <zlib.h>

struct FileEntry {
    std::string name;
    uint32_t offset;
    uint32_t uncompressed_size;
    uint32_t compressed_size;
    bool is_compressed;
    std::vector<uint32_t> decompressed_chunk_sizes;
    uint32_t size() const { return uncompressed_size; }
};

class BNKReader {
public:
    explicit BNKReader(const std::string& path) {
        _fh.open(path, std::ios::binary);
        if(!_fh) throw std::runtime_error("open failed");
        _fh.seekg(0, std::ios::end);
        _size = static_cast<uint64_t>(_fh.tellg());
        _fh.seekg(0, std::ios::beg);
        uint8_t head[8];
        read_exact(head, 8);
        _fh.seekg(0, std::ios::beg);
        uint32_t header_offset = be_u32(head);
        uint32_t ver = be_u32(head+4);
        if (ver == 2) {
            _is_v2 = true;
            read_header_v2(header_offset);
        } else {
            _is_v2 = false;
            read_header_continuous_stream();
        }
        if (_file_table_blob.empty()) throw std::runtime_error("Failed to read BNK header (decompressed file table is empty).");
        parse_tables();
    }

    const std::vector<FileEntry>& list_files() const { return file_entries; }

    void extract_file(const std::string& name, const std::string& out_path) {
        const FileEntry* entry = nullptr;
        for (auto& e : file_entries) if (e.name == name) { entry = &e; break; }
        if (!entry) throw std::runtime_error("file not found");
        std::filesystem::path p(out_path);
        std::filesystem::create_directories(p.parent_path());
        std::ofstream out(out_path, std::ios::binary);
        if (!out) throw std::runtime_error("open out failed");
        extract_entry_to(*entry, out);
    }

    void extract_all(const std::filesystem::path& out_dir) {
        std::filesystem::create_directories(out_dir);
        for (auto& e : file_entries) {
            std::filesystem::path target = out_dir / (e.name.empty() ? hex_name(e.offset) : e.name);
            std::filesystem::create_directories(target.parent_path());
            std::ofstream out(target, std::ios::binary);
            if (!out) throw std::runtime_error("open out failed");
            extract_entry_to(e, out);
        }
    }

    void close() { if (_fh.is_open()) _fh.close(); }
    ~BNKReader() { close(); }

private:
    std::ifstream _fh;
    uint64_t _size = 0;
    uint32_t base_offset = 16;
    uint8_t compress_file_data = 0;
    std::vector<uint8_t> _file_table_blob;
    std::vector<FileEntry> file_entries;
    bool _is_v2 = false;

    static uint32_t be_u32(const uint8_t* p) {
        return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|uint32_t(p[3]);
    }

    void read_exact(void* dst, size_t n) {
        _fh.read(reinterpret_cast<char*>(dst), std::streamsize(n));
        if (size_t(_fh.gcount()) != n) throw std::runtime_error("Premature EOF");
    }

    uint32_t read_u32_be() {
        uint8_t b[4]; read_exact(b,4); return be_u32(b);
    }
    uint8_t read_u8() {
        uint8_t b; read_exact(&b,1); return b;
    }

    static void inflate_init(z_stream& z, int wbits) {
        std::memset(&z, 0, sizeof(z));
        if (inflateInit2(&z, wbits) != Z_OK) throw std::runtime_error("inflateInit2 fail");
    }

    static std::vector<uint8_t> inflate_header_chunks_as_one_stream(const std::vector<std::tuple<uint32_t,uint32_t,std::vector<uint8_t>>>& chunks, int wbits) {
        z_stream z; inflate_init(z, wbits);
        std::vector<uint8_t> out;
        for (auto& t : chunks) {
            const uint32_t dsz = std::get<1>(t);
            const std::vector<uint8_t>& comp = std::get<2>(t);
            z.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(comp.data()));
            z.avail_in = (uInt)comp.size();
            if (dsz > 0) {
                size_t need = dsz;
                size_t start = out.size();
                out.resize(start + need);
                z.next_out = reinterpret_cast<Bytef*>(out.data() + start);
                z.avail_out = (uInt)need;
                int ret = inflate(&z, Z_NO_FLUSH);
                if (ret != Z_OK && ret != Z_STREAM_END) { inflateEnd(&z); throw std::runtime_error("inflate fail"); }
                size_t produced = need - z.avail_out;
                if (produced < need) {
                    size_t rem = need - produced;
                    z.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(""));
                    z.avail_in = 0;
                    z.next_out = reinterpret_cast<Bytef*>(out.data() + start + produced);
                    z.avail_out = (uInt)rem;
                    int ret2 = inflate(&z, Z_NO_FLUSH);
                    if (ret2 != Z_OK && ret2 != Z_STREAM_END) { inflateEnd(&z); throw std::runtime_error("inflate fail"); }
                    produced = need - z.avail_out;
                }
                if (produced != need) { inflateEnd(&z); throw std::runtime_error("chunk size mismatch"); }
            } else {
                for (;;) {
                    size_t before = out.size();
                    out.resize(before + 65536);
                    z.next_out = reinterpret_cast<Bytef*>(out.data() + before);
                    z.avail_out = 65536;
                    int ret = inflate(&z, Z_NO_FLUSH);
                    size_t produced = 65536 - z.avail_out;
                    out.resize(before + produced);
                    if ((ret == Z_OK && z.avail_in == 0) || ret == Z_STREAM_END) break;
                    if (ret == Z_BUF_ERROR) continue;
                    if (ret != Z_OK) { inflateEnd(&z); throw std::runtime_error("inflate fail"); }
                }
            }
        }
        for (;;) {
            size_t before = out.size();
            out.resize(before + 65536);
            z.next_out = reinterpret_cast<Bytef*>(out.data() + before);
            z.avail_out = 65536;
            z.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(""));
            z.avail_in = 0;
            int ret = inflate(&z, Z_FINISH);
            size_t produced = 65536 - z.avail_out;
            out.resize(before + produced);
            if (ret == Z_STREAM_END) break;
            if (ret == Z_BUF_ERROR && produced == 0) break;
            if (ret != Z_OK && ret != Z_BUF_ERROR) { inflateEnd(&z); throw std::runtime_error("inflate tail fail"); }
        }
        inflateEnd(&z);
        return out;
    }

    void read_header_continuous_stream() {
        base_offset = read_u32_be();
        read_u32_be();
        compress_file_data = read_u8();
        std::vector<std::tuple<uint32_t,uint32_t,std::vector<uint8_t>>> chunks;
        for (;;) {
            uint32_t comp_size = read_u32_be();
            uint32_t decomp_size = read_u32_be();
            if (comp_size == 0) break;
            std::vector<uint8_t> comp(comp_size);
            read_exact(comp.data(), comp.size());
            chunks.emplace_back(comp_size, decomp_size, std::move(comp));
        }
        for (int wbits : {15, -15, 31}) {
            try {
                _file_table_blob = inflate_header_chunks_as_one_stream(chunks, wbits);
                return;
            } catch (...) {}
        }
        if (!chunks.empty()) {
            bool ok = true;
            for (auto& c : chunks) {
                uint32_t cs = std::get<0>(c), ds = std::get<1>(c);
                if (ds != 0 && cs != ds) { ok = false; break; }
            }
            if (ok) {
                _file_table_blob.clear();
                for (auto& c : chunks) {
                    auto& v = std::get<2>(c);
                    _file_table_blob.insert(_file_table_blob.end(), v.begin(), v.end());
                }
                return;
            }
        }
        throw std::runtime_error("Header chunk decompress failed");
    }

    void read_header_v2(uint32_t file_table_offset) {
        _fh.seekg(8, std::ios::beg);
        compress_file_data = read_u8() != 0 ? 1 : 0;
        uint8_t pad[7]; read_exact(pad,7);
        base_offset = 0;
        std::vector<std::tuple<uint64_t,uint32_t,uint32_t>> metas;
        uint64_t cur = file_table_offset;
        while (cur + 8 <= _size) {
            _fh.seekg(cur, std::ios::beg);
            uint32_t comp = read_u32_be();
            uint32_t uncomp = read_u32_be();
            if (comp == 0) break;
            uint64_t data_off = cur + 8;
            if (data_off + comp > _size) break;
            metas.emplace_back(data_off, comp, uncomp);
            cur = data_off + comp;
        }
        if (metas.empty()) {
            _file_table_blob.resize(4);
            _file_table_blob[0]=0;_file_table_blob[1]=0;_file_table_blob[2]=0;_file_table_blob[3]=0;
            return;
        }
        for (int wbits : {15, -15, 31}) {
            try {
                z_stream z; inflate_init(z, wbits);
                std::vector<uint8_t> out;
                for (auto& m : metas) {
                    uint64_t off = std::get<0>(m); uint32_t comp = std::get<1>(m);
                    _fh.seekg(off, std::ios::beg);
                    std::vector<uint8_t> buf(comp);
                    read_exact(buf.data(), buf.size());
                    z.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(buf.data()));
                    z.avail_in = (uInt)buf.size();
                    for (;;) {
                        size_t before = out.size();
                        out.resize(before + 65536);
                        z.next_out = reinterpret_cast<Bytef*>(out.data() + before);
                        z.avail_out = 65536;
                        int ret = inflate(&z, Z_NO_FLUSH);
                        size_t produced = 65536 - z.avail_out;
                        out.resize(before + produced);
                        if ((ret == Z_OK && z.avail_in == 0) || ret == Z_STREAM_END) break;
                        if (ret == Z_BUF_ERROR) continue;
                        if (ret != Z_OK) { inflateEnd(&z); throw std::runtime_error("inflate fail"); }
                    }
                }
                for (;;) {
                    size_t before = out.size();
                    out.resize(before + 65536);
                    z.next_out = reinterpret_cast<Bytef*>(out.data() + before);
                    z.avail_out = 65536;
                    z.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(""));
                    z.avail_in = 0;
                    int ret = inflate(&z, Z_FINISH);
                    size_t produced = 65536 - z.avail_out;
                    out.resize(before + produced);
                    if (ret == Z_STREAM_END) break;
                    if (ret == Z_BUF_ERROR && produced == 0) break;
                    if (ret != Z_OK && ret != Z_BUF_ERROR) { inflateEnd(&z); throw std::runtime_error("inflate tail fail"); }
                }
                inflateEnd(&z);
                _file_table_blob.swap(out);
                return;
            } catch (...) {}
        }
        throw std::runtime_error("V2: file-table decompression failed");
    }

    void parse_tables() {
        size_t pos = 0;
        auto r_u32 = [&](uint32_t& v){ if (pos+4 > _file_table_blob.size()) throw std::runtime_error("EOF"); v = be_u32(&_file_table_blob[pos]); pos+=4; };
        auto r_name = [&]()->std::string{
            uint32_t n; r_u32(n);
            if (n > 1000000) throw std::runtime_error("Unreasonable name length");
            if (pos+n > _file_table_blob.size()) throw std::runtime_error("EOF");
            std::string s(reinterpret_cast<const char*>(&_file_table_blob[pos]), n);
            pos+=n;
            if (!s.empty() && s.back()==char(0)) s.pop_back();
            return s;
        };
        auto make_offset = [&](uint32_t rel){ return base_offset + rel; };
        uint32_t file_count=0; r_u32(file_count);
        std::vector<FileEntry> entries; entries.reserve(file_count);
        if (compress_file_data == 1) {
            for (uint32_t i=0;i<file_count;++i){
                std::string name=r_name();
                uint32_t rel_off; r_u32(rel_off);
                uint32_t decomp_size; r_u32(decomp_size);
                uint32_t comp_size; r_u32(comp_size);
                uint32_t chunk_count; r_u32(chunk_count);
                std::vector<uint32_t> chunks; chunks.reserve(chunk_count);
                for (uint32_t j=0;j<chunk_count;++j){ uint32_t v; r_u32(v); chunks.push_back(v); }
                FileEntry fe{ name, make_offset(rel_off), decomp_size, comp_size, true, std::move(chunks) };
                entries.push_back(std::move(fe));
            }
        } else {
            for (uint32_t i=0;i<file_count;++i){
                std::string name=r_name();
                uint32_t rel_off; r_u32(rel_off);
                uint32_t size; r_u32(size);
                FileEntry fe{ name, make_offset(rel_off), size, 0, false, {} };
                entries.push_back(std::move(fe));
            }
        }
        file_entries.swap(entries);
    }

    void extract_entry_to(const FileEntry& e, std::ofstream& out) {
        _fh.seekg(e.offset, std::ios::beg);

        if (!e.is_compressed) {
            std::vector<uint8_t> buf(e.uncompressed_size);
            read_exact(buf.data(), buf.size());
            out.write(reinterpret_cast<const char*>(buf.data()), std::streamsize(buf.size()));
            return;
        }

        std::vector<uint8_t> comp_blob(e.compressed_size);
        read_exact(comp_blob.data(), comp_blob.size());

        const size_t CHUNK_SIZE = 0x8000;  // 32KB

        for (size_t i = 0; i < e.decompressed_chunk_sizes.size(); ++i) {
            uint32_t out_len = e.decompressed_chunk_sizes[i];

            size_t comp_offset = i * CHUNK_SIZE;
            size_t comp_available = comp_blob.size() - comp_offset;
            size_t comp_size = std::min(CHUNK_SIZE, comp_available);

            if (comp_offset >= comp_blob.size()) {
                throw std::runtime_error("Invalid chunk offset");
            }

            std::optional<std::vector<uint8_t>> chunk;

            for (int wbits : {15, -15, 31}) {
                try {
                    z_stream z;
                    memset(&z, 0, sizeof(z));

                    if (inflateInit2(&z, wbits) != Z_OK) {
                        continue;
                    }

                    z.next_in = const_cast<Bytef*>(comp_blob.data() + comp_offset);
                    z.avail_in = (uInt)comp_size;

                    std::vector<uint8_t> outbuf(out_len);
                    z.next_out = outbuf.data();
                    z.avail_out = (uInt)outbuf.size();

                    int ret = inflate(&z, Z_SYNC_FLUSH);

                    size_t produced = outbuf.size() - z.avail_out;
                    inflateEnd(&z);

                    if (ret == Z_OK || ret == Z_STREAM_END) {
                        if (produced != out_len) {
                            outbuf.resize(out_len, 0);
                        }
                        chunk = std::move(outbuf);
                        break;
                    }
                } catch (...) {
                    // try next wbits value
                }
            }

            if (!chunk.has_value()) {
                throw std::runtime_error("Failed to inflate chunk");
            }

            out.write(reinterpret_cast<const char*>(chunk->data()), std::streamsize(chunk->size()));
        }
    }

    static std::string hex_name(uint32_t off) {
        char buf[32]; std::snprintf(buf,sizeof(buf),"file_%08X.bin",off); return std::string(buf);
    }
};
