bool gzip_inflate(const std::vector<uint8_t>& in,
                  std::vector<uint8_t>& out) {
    z_stream z{};
    if (inflateInit2(&z, 15 + 32) != Z_OK) return false;
    z.next_in = const_cast<Bytef*>(in.data());
    z.avail_in = (uInt)in.size();
    out.clear();
    std::vector<uint8_t> buf(1u << 16);
    int ret = Z_OK;
    while (ret != Z_STREAM_END) {
        z.next_out = buf.data();
        z.avail_out = (uInt)buf.size();
        ret = inflate(&z, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            inflateEnd(&z);
            return false;
        }
        out.insert(out.end(), buf.data(),
                   buf.data() + (buf.size() - z.avail_out));
        if (ret != Z_STREAM_END && z.avail_in == 0) {
            inflateEnd(&z);
            return false;
        }
    }
    inflateEnd(&z);
    return true;
}

bool gzip_deflate(const std::vector<uint8_t>& in,
                  std::vector<uint8_t>& out) {
    z_stream z{};
    if (deflateInit2(&z, Z_BEST_COMPRESSION, Z_DEFLATED, 15 + 16, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK) return false;
    z.next_in = const_cast<Bytef*>(in.data());
    z.avail_in = (uInt)in.size();
    out.resize((size_t)deflateBound(&z, (uLong)in.size()) + 64);
    z.next_out = out.data();
    z.avail_out = (uInt)out.size();
    const int ret = deflate(&z, Z_FINISH);
    const bool ok = (ret == Z_STREAM_END);
    out.resize(out.size() - z.avail_out);
    deflateEnd(&z);
    return ok;
}

void put_u32_be(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 3; i >= 0; --i) v.push_back(uint8_t(x >> (i * 8)));
}

void put_f32_be_v(std::vector<uint8_t>& v, float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    put_u32_be(v, bits);
}

bool find_type2_template(const std::vector<uint8_t>& bytes,
                         float out_vals[20]) {
    static const char kMagic[] = "LevelGraphicsFile";
    const size_t magic_len = sizeof(kMagic) - 1;
    size_t pos = magic_len + 8;
    auto rd32 = [&](size_t p, uint32_t& v) -> bool {
        if (p + 4 > bytes.size()) return false;
        v = (uint32_t(bytes[p]) << 24) | (uint32_t(bytes[p + 1]) << 16) |
            (uint32_t(bytes[p + 2]) << 8) | uint32_t(bytes[p + 3]);
        return true;
    };
    auto skip_cstr = [&](size_t& p) -> bool {
        while (p < bytes.size() && bytes[p] != 0) ++p;
        if (p >= bytes.size()) return false;
        ++p;
        return true;
    };
    for (int guard = 0; guard < 4096; ++guard) {
        uint32_t t = 0;
        if (!rd32(pos, t)) return false;
        pos += 4;
        if (t == 2) {
            for (int k = 0; k < 4; ++k) {
                if (!skip_cstr(pos)) return false;
            }
            uint32_t n = 0;
            if (!rd32(pos, n) || n == 0 || n > 100000) return false;
            pos += 4;
            const size_t vals = pos + 3 + 8;
            if (vals + 80 > bytes.size()) return false;
            for (int k = 0; k < 20; ++k) {
                uint32_t bits = 0;
                rd32(vals + (size_t)k * 4, bits);
                std::memcpy(&out_vals[k], &bits, 4);
            }
            return true;
        } else if (t == 4) {
            if (!skip_cstr(pos)) return false;
            pos += 8;
        } else if (t == 5 || t == 32) {
            if (!skip_cstr(pos)) return false;
        } else {
            return false;
        }
    }
    return false;
}
