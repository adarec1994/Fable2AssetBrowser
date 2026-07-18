
struct EditEntry {
    float delta[3] = {0, 0, 0};
    float rot_deg[3] = {0, 0, 0};
    float orig[3] = {0, 0, 0};
    float orig_rot[3] = {0, 0, 0};
    uint32_t lev_off = 0;
    uint8_t lev_kind = 0;
    uint32_t gdb_off[3] = {0, 0, 0};
    uint32_t gdb_rot_off[3] = {0, 0, 0};
    uint32_t gdb_entity_hash = 0;
    bool registered = false;
    bool deleted = false;

    bool moved() const {
        return delta[0] != 0.0f || delta[1] != 0.0f || delta[2] != 0.0f;
    }
    bool rotated() const {
        return rot_deg[0] != 0.0f || rot_deg[1] != 0.0f ||
               rot_deg[2] != 0.0f;
    }
    bool changed() const { return moved() || rotated() || deleted; }
};

struct FileTarget {
    std::string bnk_path;
    int         file_index = -1;
    std::string file_path;
    uint64_t disk_offset = 0;
    uint32_t on_disk_size = 0;
    bool compressed = false;
    bool in_iso = false;
    bool valid = false;
};

struct UndoState {
    float delta[3];
    float rot_deg[3];
    bool deleted;
};

struct UndoStep {
    std::vector<std::pair<uint32_t, UndoState>> before;
};

struct ModuleState {
    bool available = false;
    bool enabled   = false;
    bool dirty     = false;
    bool saving    = false;
    uint64_t revision = 0;

    FlatAssetEntry entry{};
    FileTarget lev;
    FileTarget gdb;

    std::unordered_map<uint32_t, EditEntry> edits;
    std::vector<UndoStep> undo_stack;
    std::vector<Addition> additions;

    std::unordered_map<uint32_t, std::vector<uint32_t>> contents_edits;
    std::unordered_map<uint32_t, uint32_t> contents_loot_edits;
    std::unordered_map<uint32_t, std::string> text_edits;
    std::vector<GeneratorAddition> generators;
    struct SpawnPointAdd {
        uint32_t generator_entity = 0;
        uint32_t spawn_points_record = 0;
        float pos[3] = {0, 0, 0};
    };
    std::vector<SpawnPointAdd> spawn_point_adds;
    struct SpawnPointDelete {
        uint32_t generator_entity = 0;
        uint32_t spawn_points_record = 0;
        uint32_t spawn_point_entity = 0;
    };
    std::vector<SpawnPointDelete> spawn_point_deletes;
};

ModuleState& st() {
    static ModuleState s;
    return s;
}

std::mutex& mtx() {
    static std::mutex m;
    return m;
}

constexpr size_t kMaxUndoSteps = 128;
constexpr float kDegToRad = 0.01745329252f;

void fill_bnk_target(FileTarget& t) {
    t.valid = false;
    if (t.bnk_path.empty() || t.file_index < 0) return;
    t.in_iso = ISO::IsoMount::is_iso_path(t.bnk_path);
    try {
        const auto bc = BnkCache::get(t.bnk_path);
        const auto& files = bc.reader->list_files();
        if (t.file_index < (int)files.size()) {
            t.disk_offset = bc.reader->entry_disk_offset(t.file_index);
            t.on_disk_size = bc.reader->entry_on_disk_size(t.file_index);
            t.compressed = bc.reader->entry_is_compressed(t.file_index);
            t.valid = t.on_disk_size != 0;
        }
    } catch (...) {
    }
}

std::filesystem::path edited_levels_dir() {
    std::filesystem::path root_p(S.root_dir);
    std::error_code ec;
    if (!S.root_dir.empty() &&
        std::filesystem::is_regular_file(root_p, ec)) {
        root_p = root_p.parent_path();
    }
    if (root_p.empty()) root_p = std::filesystem::current_path();
    return root_p / "edited_levels";
}

bool write_bytes_at(const std::string& path_or_bnk,
                    uint64_t offset,
                    const uint8_t* data,
                    size_t size,
                    std::string& err) {
    if (ISO::IsoMount::is_iso_path(path_or_bnk)) {
        const std::string vpath =
            ISO::IsoMount::strip_iso_prefix(path_or_bnk);
        if (!ISO::IsoMount::instance().write_at(vpath, offset, data,
                                                size)) {
            err = "ISO in-place write failed (" + vpath + ")";
            return false;
        }
        return true;
    }
    std::fstream f(path_or_bnk,
                   std::ios::binary | std::ios::in | std::ios::out);
    if (!f) {
        err = "could not open " + path_or_bnk + " for writing";
        return false;
    }
    f.seekp((std::streamoff)offset);
    f.write(reinterpret_cast<const char*>(data), (std::streamsize)size);
    if (!f) {
        err = "write failed at offset " + std::to_string(offset);
        return false;
    }
    return true;
}

void put_f32_be(uint8_t* p, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    p[0] = uint8_t(bits >> 24);
    p[1] = uint8_t(bits >> 16);
    p[2] = uint8_t(bits >> 8);
    p[3] = uint8_t(bits);
}

bool patch_target(const FileTarget& t, uint32_t payload_off,
                  const float* vals, int count, std::string& err) {
    std::vector<uint8_t> buf((size_t)count * 4);
    for (int i = 0; i < count; ++i) put_f32_be(buf.data() + i * 4, vals[i]);
    if (!t.file_path.empty()) {
        return write_bytes_at(t.file_path, payload_off, buf.data(),
                              buf.size(), err);
    }
    return write_bytes_at(t.bnk_path, t.disk_offset + payload_off,
                          buf.data(), buf.size(), err);
}

bool target_patchable_in_place(const FileTarget& t) {
    if (!t.file_path.empty()) return true;
    return t.valid && !t.compressed;
}

void put_u64_be(std::vector<uint8_t>& v, uint64_t x) {
    for (int i = 7; i >= 0; --i) v.push_back(uint8_t(x >> (i * 8)));
}

uint32_t get_u32_be2(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

uint32_t fnv1_32(const std::string& s) {
    uint32_t h = 0x811C9DC5u;
    for (unsigned char c : s) {
        h *= 0x01000193u;
        h ^= c;
    }
    return h;
}

std::string lower_model_path(const std::string& p) {
    std::string mp = p;
    for (char& c : mp) {
        if (c == '/') c = '\\';
        else c = (char)std::tolower((unsigned char)c);
    }
    return mp;
}

uint64_t addition_instance_hash(const std::string& lowered, size_t ai) {
    const uint32_t hi = fnv1_32(lowered);
    const uint32_t lo = fnv1_32(lowered + "#placed" + std::to_string(ai));
    return ((uint64_t)hi << 32) | lo;
}
