    Buf out;

    static const uint8_t kMagic[] = {
        'K','a','y','d','a','r','a',' ','F','B','X',' ','B','i','n','a',
        'r','y',' ',' ', 0x00, 0x1A, 0x00
    };
    out.put_bytes(kMagic, sizeof(kMagic));
    out.put_u32(7400);

    for (const auto& c : root.children) write_node(out, c);

    for (int i = 0; i < 13; ++i) out.put_u8(0);

    for (int i = 0; i < 16; ++i) out.put_u8(0);
    while (out.data.size() & 0xF) out.put_u8(0);
    out.put_u32(0);
    out.put_u32(7400);
    for (int i = 0; i < 120; ++i) out.put_u8(0);
    static const uint8_t kFooterMagic[] = {
        0xF8, 0x5A, 0x8C, 0x6A, 0xDE, 0xF5, 0xD9, 0x7E,
        0xEC, 0xE9, 0x0C, 0xE3, 0x75, 0x8F, 0x29, 0x0B
    };
    out.put_bytes(kFooterMagic, 16);

    std::ofstream f(fbx_path, std::ios::binary | std::ios::trunc);
    if (!f) {
        err_msg = "cannot open " + fbx_path + " for writing";
        return false;
    }
    f.write((const char*)out.data.data(), (std::streamsize)out.data.size());
    if (!f.good()) {
        err_msg = "write failed for " + fbx_path;
        return false;
    }
    return true;
}
