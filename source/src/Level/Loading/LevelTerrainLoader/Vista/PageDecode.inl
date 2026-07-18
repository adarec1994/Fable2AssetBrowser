static bool decode_bg_page(const std::vector<uint8_t>& ehf, uint32_t off,
                           std::vector<uint8_t>& rgba, int& w, int& h)
{
    if (uint64_t(off) + 0x60 > ehf.size()) return false;
    if (ehf_be32(ehf, off) != 0xFFFFFFFEu) return false;
    if (ehf_be32(ehf, off + 0x18) != 35u) return false;
    const uint32_t mt = ehf_be32(ehf, off + 0x20);
    if (mt < 0x54 || mt > 0x200 || size_t(off) + mt + 8 > ehf.size()) {
        return false;
    }
    const uint32_t comp = ehf_be32(ehf, off + mt + 4);
    size_t blob_end = size_t(off) + mt + 8 + comp;
    if (blob_end > ehf.size()) blob_end = ehf.size();
    std::vector<uint8_t> blob(ehf.begin() + off, ehf.begin() + blob_end);
    TextureAtlas::DecodedAtlas dec = TextureAtlas::DecodeAtlas(blob);
    if (!dec.ok || dec.rgba.empty()) return false;
    rgba = std::move(dec.rgba);
    w = dec.width;
    h = dec.height;
    return true;
}
