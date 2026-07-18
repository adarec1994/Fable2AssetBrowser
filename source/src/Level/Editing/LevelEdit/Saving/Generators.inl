uint32_t create_generator_transform(GdbEdit::GdbFile& g,
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
    constexpr uint32_t kDefaultTransformParent = 0xFD37C2F6u;
    constexpr uint32_t kDefaultPositionParent = 0xFC1909D4u;
    constexpr uint32_t kDefaultRotationParent = 0xB3E58682u;
    auto fbits = [](float value) {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    };
    auto vec3_record = [&](float x, float y, float z,
                           uint32_t parent) -> uint32_t {
        const uint32_t hash = g.AllocRecordHash();
        std::vector<GdbEdit::Field> fields;
        GdbEdit::Field field;
        field.hash = kVecZ; field.type = 3;
        field.value = fbits(z); field.decl = 3;
        fields.push_back(field);
        field.hash = kVecY; field.value = fbits(y); field.decl = 2;
        fields.push_back(field);
        field.hash = kVecX; field.value = fbits(x); field.decl = 1;
        fields.push_back(field);
        field.hash = kParent; field.type = 6;
        field.value = parent; field.decl = 0;
        fields.push_back(field);
        return g.AddRecord(hash, fields, 1) ? hash : 0;
    };

    const uint32_t pos_record = vec3_record(
        pos[0], pos[1], pos[2],
        d.gen_position_parent ? d.gen_position_parent
                              : kDefaultPositionParent);
    const uint32_t rot_record = vec3_record(
        0, 0, 0,
        d.gen_rotation_parent ? d.gen_rotation_parent
                              : kDefaultRotationParent);
    if (!pos_record || !rot_record) {
        err = "generator transform append failed";
        return 0;
    }

    const uint32_t transform = g.AllocRecordHash();
    std::vector<GdbEdit::Field> fields;
    GdbEdit::Field field;
    field.hash = kRotation; field.type = 6;
    field.value = rot_record; field.decl = 1;
    fields.push_back(field);
    field.hash = kParent;
    field.value = d.gen_transform_parent ? d.gen_transform_parent
                                         : kDefaultTransformParent;
    field.decl = 2;
    fields.push_back(field);
    field.hash = kPosition;
    field.value = pos_record; field.decl = 0;
    fields.push_back(field);
    if (!g.AddRecord(transform, fields, 1)) {
        err = "generator transform comp append failed";
        return 0;
    }
    return transform;
}

uint32_t create_generator_families(GdbEdit::GdbFile& g,
                                   const std::string& creature_name,
                                   uint32_t creature_entity,
                                   std::string& err)
{
    constexpr uint32_t kParent = 0x5F6317D5u;
    constexpr uint32_t kCreatures = 0xA1F7A17Du;
    constexpr uint32_t kFamiliesBase = 0x54C2CFF7u;
    constexpr uint32_t kFamilyBase = 0x751EBC11u;
    constexpr uint32_t kCreaturesBase = 0x731AB342u;
    if (!creature_entity || creature_name.empty()) {
        err = "selected creature has no GDB entity definition";
        return 0;
    }

    const uint32_t creature_field = fnv1_32(creature_name);
    g.AddDictString(creature_field, creature_name);

    const uint32_t creatures = g.AllocRecordHash();
    {
        std::vector<GdbEdit::Field> fields;
        GdbEdit::Field field;
        field.hash = kParent; field.type = 6;
        field.value = kCreaturesBase; field.decl = 0;
        fields.push_back(field);
        field.hash = creature_field; field.type = 7;
        field.value = creature_entity; field.decl = 1;
        fields.push_back(field);
        if (!g.AddRecord(creatures, fields, 1)) {
            err = "generator creature list append failed";
            return 0;
        }
    }

    const uint32_t family = g.AllocRecordHash();
    {
        std::vector<GdbEdit::Field> fields;
        GdbEdit::Field field;
        field.hash = kParent; field.type = 6;
        field.value = kFamilyBase; field.decl = 0;
        fields.push_back(field);
        field.hash = kCreatures; field.type = 6;
        field.value = creatures; field.decl = 1;
        fields.push_back(field);
        if (!g.AddRecord(family, fields, 1)) {
            err = "generator family append failed";
            return 0;
        }
    }

    const uint32_t families = g.AllocRecordHash();
    {
        std::vector<GdbEdit::Field> fields;
        GdbEdit::Field field;
        field.hash = kParent; field.type = 6;
        field.value = kFamiliesBase; field.decl = 0;
        fields.push_back(field);
        field.hash = creature_field; field.type = 6;
        field.value = family; field.decl = 1;
        fields.push_back(field);
        if (!g.AddRecord(families, fields, 1)) {
            err = "generator families append failed";
            return 0;
        }
    }
    return families;
}

bool creature_catalog_entity(const std::string& name, uint32_t& entity_hash)
{
    for (const auto& creature : g_level_creature_catalog) {
        if (creature.name == name && creature.entity_hash != 0) {
            entity_hash = creature.entity_hash;
            return true;
        }
    }
    return false;
}

bool legacy_generator_data(const GdbEdit::GdbFile& g,
                           const Gdb::SpawnDonorInfo& d,
                           uint32_t entity_hash,
                           uint32_t& component_hash,
                           uint32_t& list_hash,
                           std::string& creature_name,
                           uint32_t& creature_entity,
                           float out_pos[3])
{
    constexpr uint32_t kNull = 0x811C9DC5u;
    constexpr uint32_t kSpawnedCreatureName = 0x2A80DD7Bu;
    constexpr uint32_t kSpawnPoints = 0x559B5DBFu;
    constexpr uint32_t kFamilies = 0xF44CE155u;
    constexpr uint32_t kPosition = 0xBD7C27D4u;
    constexpr uint32_t kVec[3] = {
        0x050C5D47u, 0x050C5D46u, 0x050C5D45u,
    };
    creature_name.clear();
    creature_entity = 0;
    GdbEdit::Field field;
    if (!g.FindLocalField(entity_hash, d.gen_comp_field, field) ||
        field.type != 6) {
        return false;
    }
    component_hash = field.value;

    GdbEdit::Field families;
    const bool has_families =
        g.FindLocalField(component_hash, kFamilies, families) &&
        families.type == 6 && families.value != 0 &&
        families.value != kNull;
    GdbEdit::Field spawned_name;
    if (g.FindLocalField(component_hash, kSpawnedCreatureName,
                         spawned_name)) {
        const auto it = g.Dict().find(spawned_name.value);
        if (it != g.Dict().end()) {
            uint32_t catalog_entity = 0;
            if (creature_catalog_entity(it->second, catalog_entity)) {
                creature_name = it->second;
                creature_entity = catalog_entity;
            }
        }
    }
    const bool old_schema = g.SchemaHeaderLow(component_hash) == 0;
    const bool missing_creature_family =
        !has_families && creature_entity != 0;
    if (!old_schema && !missing_creature_family) {
        return false;
    }

    if (!g.FindLocalField(component_hash, kSpawnPoints, field) ||
        field.type != 6) {
        return false;
    }
    list_hash = field.value;
    if (!g.FindLocalField(entity_hash, d.gen_transform_field, field) ||
        field.type != 6) {
        return false;
    }
    GdbEdit::Field position;
    if (!g.FindLocalField(field.value, kPosition, position) ||
        position.type != 6) {
        return false;
    }
    for (size_t i = 0; i < 3; ++i) {
        GdbEdit::Field value;
        if (!g.FindLocalField(position.value, kVec[i], value) ||
            value.type != 3) {
            return false;
        }
        std::memcpy(&out_pos[i], &value.value, sizeof(float));
    }
    return true;
}

bool has_legacy_generators(const GdbEdit::GdbFile& g,
                           const Gdb::SpawnDonorInfo& d)
{
    if (!d.valid()) return false;
    uint32_t component = 0, list = 0;
    uint32_t creature_entity = 0;
    std::string creature_name;
    float pos[3];
    for (size_t i = 0; i < g.RecordCount(); ++i) {
        if (legacy_generator_data(g, d, g.RecordAt(i).hash,
                                  component, list, creature_name,
                                  creature_entity, pos)) {
            return true;
        }
    }
    return false;
}

bool repair_legacy_generators(GdbEdit::GdbFile& g,
                              const Gdb::SpawnDonorInfo& d,
                              size_t& repaired,
                              std::string& err)
{
    constexpr uint32_t kParent = 0x5F6317D5u;
    constexpr uint32_t kSpawnedCreatureName = 0x2A80DD7Bu;
    constexpr uint32_t kSpawnPoints = 0x559B5DBFu;
    constexpr uint32_t kFamilies = 0xF44CE155u;
    struct Candidate {
        uint32_t entity = 0;
        uint32_t component = 0;
        uint32_t list = 0;
        std::string creature_name;
        uint32_t creature_entity = 0;
        float pos[3] = {};
    };
    std::vector<Candidate> candidates;
    const size_t original_count = g.RecordCount();
    for (size_t i = 0; i < original_count; ++i) {
        Candidate candidate;
        candidate.entity = g.RecordAt(i).hash;
        if (legacy_generator_data(
                g, d, candidate.entity, candidate.component,
                candidate.list, candidate.creature_name,
                candidate.creature_entity, candidate.pos)) {
            candidates.push_back(candidate);
        }
    }

    for (const Candidate& candidate : candidates) {
        std::vector<GdbEdit::Field> list_fields;
        std::vector<GdbEdit::Field> component_fields;
        if (!g.Fields(g.FindRecord(candidate.list), list_fields) ||
            !g.Fields(g.FindRecord(candidate.component), component_fields)) {
            err = "legacy generator records are unreadable";
            return false;
        }
        const uint32_t new_list = g.AllocRecordHash();
        if (!g.AddRecord(new_list, list_fields, 1)) {
            err = "legacy generator spawn list migration failed";
            return false;
        }
        uint32_t families = 0;
        if (candidate.creature_entity != 0) {
            families = create_generator_families(
                g, candidate.creature_name, candidate.creature_entity, err);
            if (!families) return false;
        }
        bool found_families = false;
        uint32_t next_decl = 4;
        for (GdbEdit::Field& field : component_fields) {
            if (field.hash == kParent) {
                field.decl = 0;
            } else if (field.hash == kSpawnPoints) {
                field.value = new_list;
                field.decl = 1;
            } else if (field.hash == kSpawnedCreatureName) {
                field.decl = 2;
            } else if (field.hash == kFamilies) {
                if (families) field.value = families;
                field.decl = 3;
                found_families = true;
            } else {
                field.decl = next_decl++;
            }
        }
        if (!found_families && families) {
            GdbEdit::Field family_field;
            family_field.hash = kFamilies;
            family_field.type = 6;
            family_field.value = families;
            family_field.decl = 3;
            component_fields.push_back(family_field);
        }
        const uint32_t new_component = g.AllocRecordHash();
        if (!g.AddRecord(new_component, component_fields, 1)) {
            err = "legacy generator component migration failed";
            return false;
        }
        const uint32_t new_transform = create_generator_transform(
            g, d, candidate.pos, err);
        if (!new_transform ||
            !g.SetFieldValue(candidate.entity, d.gen_comp_field,
                             new_component) ||
            !g.SetFieldValue(candidate.entity, d.gen_transform_field,
                             new_transform)) {
            if (err.empty()) err = "legacy generator migration failed";
            return false;
        }
        ++repaired;
    }
    return true;
}

uint32_t create_generator_entity(
    GdbEdit::GdbFile& g,
    const Gdb::SpawnDonorInfo& d,
    const GeneratorAddition& ga,
    std::vector<std::pair<std::string, uint32_t>>& new_save_entities,
    std::string& err)
{
    constexpr uint32_t kParent = 0x5F6317D5u;
    constexpr uint32_t kSpawnedCreatureName = 0x2A80DD7Bu;
    constexpr uint32_t kSpawnPoints = 0x559B5DBFu;
    constexpr uint32_t kFamilies = 0xF44CE155u;
    constexpr uint32_t kDefaultGeneratorCompParent = 0x9B6881DAu;
    constexpr uint32_t kDefaultSpawnListParent = 0x2FAB69BFu;

    std::vector<uint32_t> sp_entities;
    for (const auto& p : ga.spawn_points) {
        const float pp[3] = {p[0], p[1], p[2]};
        const uint32_t sp = create_spawn_point_entity(g, d, pp, err);
        if (!sp) return 0;
        char nm[32];
        std::snprintf(nm, sizeof(nm), "F2AB_SP_%08X", sp);
        g.AddNameMapping(nm, sp);
        new_save_entities.emplace_back(nm, sp);
        sp_entities.push_back(sp);
    }

    const uint32_t list_rec = g.AllocRecordHash();
    {
        std::vector<GdbEdit::Field> fs;
        GdbEdit::Field f;
        for (size_t i = 0; i < sp_entities.size(); ++i) {
            const std::string fname =
                "SpawnPoint" + std::to_string(i + 1);
            f.hash = fnv1_32(fname);
            f.type = 7;
            f.value = sp_entities[i];
            f.decl = uint32_t(i);
            fs.push_back(f);
        }
        f.hash = kParent; f.type = 6;
        f.value = d.spawn_list_parent ? d.spawn_list_parent
                                      : kDefaultSpawnListParent;
        f.decl = uint32_t(sp_entities.size());
        fs.push_back(f);
        if (!g.AddRecord(list_rec, fs, 1)) {
            err = "spawn list append failed";
            return 0;
        }
    }

    const uint32_t families_rec = create_generator_families(
        g, ga.creature_name, ga.creature_entity, err);
    if (!families_rec) return 0;

    const uint32_t comp_rec = g.AllocRecordHash();
    {
        std::vector<GdbEdit::Field> fs;
        GdbEdit::Field f;
        f.hash = kSpawnedCreatureName; f.type = 4;
        f.value = fnv1_32(ga.creature_name);
        f.decl = 2;
        fs.push_back(f);
        f.hash = kSpawnPoints; f.type = 6; f.value = list_rec;
        f.decl = 1;
        fs.push_back(f);
        f.hash = kParent; f.type = 6;
        f.value = d.gen_comp_parent ? d.gen_comp_parent
                                    : kDefaultGeneratorCompParent;
        f.decl = 0;
        fs.push_back(f);
        f.hash = kFamilies; f.type = 6; f.value = families_rec;
        f.decl = 3;
        fs.push_back(f);
        if (!g.AddRecord(comp_rec, fs, 1)) {
            err = "generator comp append failed";
            return 0;
        }
    }
    g.AddDictString(fnv1_32(ga.creature_name), ga.creature_name);

    const uint32_t tf_rec =
        create_generator_transform(g, d, ga.pos, err);
    if (!tf_rec) return 0;

    const uint32_t ent = g.AllocRecordHash();
    {
        std::vector<GdbEdit::Field> fs;
        GdbEdit::Field f;
        f.hash = kParent; f.type = 6; f.value = d.gen_template;
        f.decl = 0;
        fs.push_back(f);
        f.hash = d.gen_comp_field; f.type = 6; f.value = comp_rec;
        f.decl = 2;
        fs.push_back(f);
        if (tf_rec) {
            f.hash = d.gen_transform_field; f.type = 6;
            f.value = tf_rec;
            f.decl = 1;
            fs.push_back(f);
        }
        if (!g.AddRecord(ent, fs, 0)) {
            err = "generator entity append failed";
            return 0;
        }
    }
    char nm[32];
    std::snprintf(nm, sizeof(nm), "F2AB_Gen_%08X", ent);
    g.AddNameMapping(nm, ent);
    new_save_entities.emplace_back(nm, ent);
    return ent;
}
