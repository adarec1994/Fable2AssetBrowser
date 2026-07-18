uint32_t create_spawn_point_transform(GdbEdit::GdbFile& g,
                                      const Gdb::SpawnDonorInfo& d,
                                      const float pos[3],
                                      std::string& err)
{
    constexpr uint32_t kParent = 0x5F6317D5u;
    constexpr uint32_t kVecX = 0x050C5D47u;
    constexpr uint32_t kVecY = 0x050C5D46u;
    constexpr uint32_t kVecZ = 0x050C5D45u;
    constexpr uint32_t kPosition = 0xBD7C27D4u;
    constexpr uint32_t kRotation = 0x21EBC83Bu;
    constexpr uint32_t kDefaultTransformParent = 0x3E64FFF3u;
    constexpr uint32_t kDefaultPositionParent = 0x4771F72Fu;
    constexpr uint32_t kDefaultRotationParent = 0xEBB606E5u;
    auto fbits = [](float f) {
        uint32_t u;
        std::memcpy(&u, &f, 4);
        return u;
    };
    auto vec3_record = [&](float x, float y, float z,
                           uint32_t parent) -> uint32_t {
        const uint32_t h = g.AllocRecordHash();
        std::vector<GdbEdit::Field> fs;
        GdbEdit::Field f;
        f.hash = kVecZ; f.type = 3; f.value = fbits(z); f.decl = 3;
        fs.push_back(f);
        f.hash = kVecY; f.type = 3; f.value = fbits(y); f.decl = 2;
        fs.push_back(f);
        f.hash = kVecX; f.type = 3; f.value = fbits(x); f.decl = 1;
        fs.push_back(f);
        f.hash = kParent; f.type = 6; f.value = parent; f.decl = 0;
        fs.push_back(f);
        return g.AddRecord(h, fs, 1) ? h : 0;
    };
    const uint32_t pos_rec = vec3_record(
        pos[0], pos[1], pos[2],
        d.sp_position_parent ? d.sp_position_parent
                             : kDefaultPositionParent);
    const uint32_t rot_rec = vec3_record(
        0, 0, 0,
        d.sp_rotation_parent ? d.sp_rotation_parent
                             : kDefaultRotationParent);
    if (!pos_rec || !rot_rec) {
        err = "spawn point transform append failed";
        return 0;
    }
    const uint32_t comp_rec = g.AllocRecordHash();
    {
        std::vector<GdbEdit::Field> fs;
        GdbEdit::Field f;
        f.hash = kRotation; f.type = 6; f.value = rot_rec;
        f.decl = 1;
        fs.push_back(f);
        f.hash = kParent; f.type = 6;
        f.value = d.sp_transform_parent ? d.sp_transform_parent
                                        : kDefaultTransformParent;
        f.decl = 2;
        fs.push_back(f);
        f.hash = kPosition; f.type = 6; f.value = pos_rec;
        f.decl = 0;
        fs.push_back(f);
        if (!g.AddRecord(comp_rec, fs, 1)) {
            err = "spawn point transform append failed";
            return 0;
        }
    }
    return comp_rec;
}

uint32_t create_spawn_point_entity(GdbEdit::GdbFile& g,
                                   const Gdb::SpawnDonorInfo& d,
                                   const float pos[3],
                                   std::string& err)
{
    constexpr uint32_t kParent = 0x5F6317D5u;
    const uint32_t comp_rec =
        create_spawn_point_transform(g, d, pos, err);
    if (!comp_rec) return 0;

    const uint32_t ent = g.AllocRecordHash();
    {
        std::vector<GdbEdit::Field> fs;
        GdbEdit::Field f;
        f.hash = kParent; f.type = 6; f.value = d.sp_template;
        f.decl = 0;
        fs.push_back(f);
        f.hash = d.sp_transform_field; f.type = 6; f.value = comp_rec;
        f.decl = 1;
        fs.push_back(f);
        if (!g.AddRecord(ent, fs, 0)) {
            err = "spawn point entity append failed";
            return 0;
        }
    }
    return ent;
}

bool legacy_spawn_point_position(const GdbEdit::GdbFile& g,
                                 const Gdb::SpawnDonorInfo& d,
                                 uint32_t entity_hash,
                                 float out_pos[3])
{
    constexpr uint32_t kPosition = 0xBD7C27D4u;
    constexpr uint32_t kRotation = 0x21EBC83Bu;
    constexpr uint32_t kVec[3] = {
        0x050C5D47u, 0x050C5D46u, 0x050C5D45u,
    };
    GdbEdit::Field field;
    if (g.FindLocalField(entity_hash, d.sp_transform_field, field)) {
        return false;
    }
    if (!g.FindLocalField(entity_hash, d.sp_comp_field, field) ||
        field.type != 6) {
        return false;
    }
    const uint32_t malformed_component = field.value;
    GdbEdit::Field pos_field, rot_field;
    if (!g.FindLocalField(malformed_component, kPosition, pos_field) ||
        pos_field.type != 6 ||
        !g.FindLocalField(malformed_component, kRotation, rot_field) ||
        rot_field.type != 6) {
        return false;
    }
    for (size_t i = 0; i < 3; ++i) {
        GdbEdit::Field value;
        if (!g.FindLocalField(pos_field.value, kVec[i], value) ||
            value.type != 3) {
            return false;
        }
        std::memcpy(&out_pos[i], &value.value, sizeof(float));
    }
    return true;
}

bool has_legacy_spawn_points(const GdbEdit::GdbFile& g,
                             const Gdb::SpawnDonorInfo& d)
{
    if (!d.valid()) return false;
    float pos[3];
    for (size_t i = 0; i < g.RecordCount(); ++i) {
        if (legacy_spawn_point_position(
                g, d, g.RecordAt(i).hash, pos)) {
            return true;
        }
    }
    return false;
}

bool repair_legacy_spawn_points(GdbEdit::GdbFile& g,
                                const Gdb::SpawnDonorInfo& d,
                                size_t& repaired,
                                std::string& err)
{
    struct Candidate {
        uint32_t entity = 0;
        float pos[3] = {};
    };
    std::vector<Candidate> candidates;
    const size_t original_count = g.RecordCount();
    for (size_t i = 0; i < original_count; ++i) {
        Candidate candidate;
        candidate.entity = g.RecordAt(i).hash;
        if (legacy_spawn_point_position(
                g, d, candidate.entity, candidate.pos)) {
            candidates.push_back(candidate);
        }
    }

    for (const Candidate& candidate : candidates) {
        const uint32_t transform = create_spawn_point_transform(
            g, d, candidate.pos, err);
        if (!transform) return false;
        if (!g.AddField(candidate.entity, d.sp_transform_field, 6,
                        transform, 1) ||
            !g.RemoveField(candidate.entity, d.sp_comp_field)) {
            err = "legacy spawn point component migration failed";
            return false;
        }
        ++repaired;
    }
    return true;
}
