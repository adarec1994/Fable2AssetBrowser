static std::vector<EhfEmbeddedBc1Mip>
collect_ehf_embedded_bc1_primaries(const std::vector<uint8_t>& ehf)
{
    std::vector<EhfEmbeddedBc1Mip> all;
    if (ehf.size() < 63) return all;
    const uint32_t body_off  = ehf_be32(ehf, 55);
    const uint32_t body_size = ehf_be32(ehf, 59);
    const size_t body_end = size_t(body_off) + size_t(body_size);
    if (body_end > ehf.size()) return all;

    for (size_t i = body_end; i + 0x60 < ehf.size(); ++i) {
        if (ehf[i] != 0xFF || ehf[i + 1] != 0xFF ||
            ehf[i + 2] != 0xFF || ehf[i + 3] != 0xFE) continue;
        const uint32_t w  = ehf_be32(ehf, i + 0x10);
        const uint32_t h  = ehf_be32(ehf, i + 0x14);
        const uint32_t pf = ehf_be32(ehf, i + 0x18);
        const uint32_t mt = ehf_be32(ehf, i + 0x20);
        if (pf != 35u || w == 0 || h == 0 ||
            w > 8192 || h > 8192 ||
            mt < 0x54 || mt > 0x200) {
            continue;
        }
        const size_t table = i + mt;
        if (table + 8 > ehf.size()) continue;
        const uint32_t raw_size  = ehf_be32(ehf, table);
        const uint32_t comp_size = ehf_be32(ehf, table + 4);
        const size_t zlib_at = table + 8;
        if (comp_size < 2 || zlib_at + size_t(comp_size) > ehf.size()) continue;
        if (ehf[zlib_at] != 0x78) continue;
        all.push_back({i, w, h, raw_size, comp_size});
    }

    std::vector<EhfEmbeddedBc1Mip> primaries;
    for (size_t i = 0; i < all.size();) {
        if (i + 2 < all.size() &&
            all[i + 1].header_w * 2u == all[i].header_w &&
            all[i + 2].header_w * 4u == all[i].header_w)
        {
            primaries.push_back(all[i]);
            i += 3;
        } else {
            ++i;
        }
    }
    return primaries;
}

static bool decode_ehf_embedded_bc1(const std::vector<uint8_t>& ehf,
                                    const EhfEmbeddedBc1Mip& mip,
                                    std::vector<uint8_t>& rgba,
                                    int& w,
                                    int& h)
{
    rgba.clear();
    w = 0;
    h = 0;
    const uint32_t mt = ehf_be32(ehf, mip.offset + 0x20);
    const size_t table = mip.offset + mt;
    const size_t zlib_at = table + 8;
    if (zlib_at + size_t(mip.comp_size) > ehf.size()) return false;

    std::vector<uint8_t> body(mip.raw_size);
    z_stream zs{};
    zs.next_in   = const_cast<Bytef*>(ehf.data() + zlib_at);
    zs.avail_in  = (uInt)mip.comp_size;
    zs.next_out  = body.data();
    zs.avail_out = (uInt)mip.raw_size;
    const int rc_init = inflateInit2(&zs, 15);
    const int rc = (rc_init == Z_OK) ? inflate(&zs, Z_FINISH) : Z_ERRNO;
    const size_t produced = size_t(mip.raw_size) - size_t(zs.avail_out);
    inflateEnd(&zs);
    if (rc_init != Z_OK || produced != mip.raw_size ||
        !(rc == Z_STREAM_END || rc == Z_OK || rc == Z_BUF_ERROR)) {
        return false;
    }

    std::vector<uint8_t> bc1;
    std::string err;
    if (!lh_decode_compressed_mip(body.data(), body.size(),
                                  w, h, bc1, &err,
                                  false)) {
        return false;
    }
    return TextureAtlas::DecodeRawBc1ToRgba(bc1.data(), bc1.size(),
                                            w, h, rgba);
}
