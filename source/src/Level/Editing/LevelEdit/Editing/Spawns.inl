int AddGenerator(const float pos[3], const std::string& creature_name,
                 uint32_t creature_entity,
                 const std::vector<std::string>& asset_models)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (!s.available || s.saving) return -1;
    GeneratorAddition g;
    g.pos[0] = pos[0];
    g.pos[1] = pos[1];
    g.pos[2] = pos[2];
    g.creature_name = creature_name;
    g.creature_entity = creature_entity;
    if (!g.creature_entity) {
        for (const auto& creature : g_level_creature_catalog) {
            if (creature.name == creature_name) {
                g.creature_entity = creature.entity_hash;
                break;
            }
        }
    }
    g.asset_models = asset_models;
    g.spawn_points.push_back({pos[0] + 1.5f, pos[1], pos[2]});
    s.generators.push_back(std::move(g));
    s.dirty = true;
    ++s.revision;
    return int(s.generators.size()) - 1;
}

void GetGenerators(std::vector<GeneratorAddition>& out)
{
    std::lock_guard<std::mutex> lk(mtx());
    out = st().generators;
}

void MovePendingGenerator(int index, const float pos[3])
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (index < 0 || size_t(index) >= s.generators.size()) return;
    auto& g = s.generators[size_t(index)];
    g.pos[0] = pos[0];
    g.pos[1] = pos[1];
    g.pos[2] = pos[2];
    s.dirty = true;
    ++s.revision;
}

void RemoveGenerator(int index)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (index < 0 || size_t(index) >= s.generators.size()) return;
    s.generators[size_t(index)].removed = true;
    s.dirty = true;
    ++s.revision;
}

void AddGeneratorSpawnPoint(int index, const float pos[3])
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (index < 0 || size_t(index) >= s.generators.size()) return;
    s.generators[size_t(index)].spawn_points.push_back(
        {pos[0], pos[1], pos[2]});
    s.dirty = true;
    ++s.revision;
}

void RemoveGeneratorSpawnPoint(int index, int sp_index)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (index < 0 || size_t(index) >= s.generators.size()) return;
    auto& sp = s.generators[size_t(index)].spawn_points;
    if (sp_index < 0 || size_t(sp_index) >= sp.size()) return;
    sp.erase(sp.begin() + sp_index);
    s.dirty = true;
    ++s.revision;
}

void AddSpawnPointToExisting(uint32_t generator_entity,
                             uint32_t spawn_points_record,
                             const float pos[3])
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (!s.available || s.saving || !spawn_points_record) return;
    ModuleState::SpawnPointAdd a;
    a.generator_entity = generator_entity;
    a.spawn_points_record = spawn_points_record;
    a.pos[0] = pos[0];
    a.pos[1] = pos[1];
    a.pos[2] = pos[2];
    s.spawn_point_adds.push_back(a);
    s.dirty = true;
    ++s.revision;
}

void RemoveSpawnPointFromExisting(uint32_t generator_entity,
                                  uint32_t spawn_points_record,
                                  uint32_t spawn_point_entity)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (!s.available || s.saving || !spawn_points_record ||
        !spawn_point_entity) {
        return;
    }
    for (const auto& deletion : s.spawn_point_deletes) {
        if (deletion.spawn_point_entity == spawn_point_entity) return;
    }
    ModuleState::SpawnPointDelete deletion;
    deletion.generator_entity = generator_entity;
    deletion.spawn_points_record = spawn_points_record;
    deletion.spawn_point_entity = spawn_point_entity;
    s.spawn_point_deletes.push_back(deletion);
    s.dirty = true;
    ++s.revision;
}

bool SpawnPointRemovalPending(uint32_t spawn_point_entity)
{
    std::lock_guard<std::mutex> lk(mtx());
    for (const auto& deletion : st().spawn_point_deletes) {
        if (deletion.spawn_point_entity == spawn_point_entity) return true;
    }
    return false;
}

size_t PendingSpawnPointCount()
{
    std::lock_guard<std::mutex> lk(mtx());
    return st().spawn_point_adds.size();
}

void GetPendingSpawnPoints(std::vector<PendingSpawnPoint>& out)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    out.clear();
    for (size_t gi = 0; gi < s.generators.size(); ++gi) {
        const auto& g = s.generators[gi];
        if (g.removed) continue;
        for (size_t si = 0; si < g.spawn_points.size(); ++si) {
            PendingSpawnPoint p;
            p.id = int((gi << 8) | si);
            p.pos[0] = g.spawn_points[si][0];
            p.pos[1] = g.spawn_points[si][1];
            p.pos[2] = g.spawn_points[si][2];
            p.label = "new spawn point (" + g.creature_name + ")";
            out.push_back(std::move(p));
        }
    }
    for (size_t i = 0; i < s.spawn_point_adds.size(); ++i) {
        PendingSpawnPoint p;
        p.id = 0x1000000 + int(i);
        p.pos[0] = s.spawn_point_adds[i].pos[0];
        p.pos[1] = s.spawn_point_adds[i].pos[1];
        p.pos[2] = s.spawn_point_adds[i].pos[2];
        p.label = "new spawn point";
        out.push_back(std::move(p));
    }
}

void MovePendingSpawnPoint(int id, const float pos[3])
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (id >= 0x1000000) {
        const size_t i = size_t(id - 0x1000000);
        if (i >= s.spawn_point_adds.size()) return;
        s.spawn_point_adds[i].pos[0] = pos[0];
        s.spawn_point_adds[i].pos[1] = pos[1];
        s.spawn_point_adds[i].pos[2] = pos[2];
    } else {
        const size_t gi = size_t(id) >> 8;
        const size_t si = size_t(id) & 0xFF;
        if (gi >= s.generators.size() ||
            si >= s.generators[gi].spawn_points.size()) {
            return;
        }
        s.generators[gi].spawn_points[si] = {pos[0], pos[1], pos[2]};
    }
    s.dirty = true;
    ++s.revision;
}

void RemovePendingSpawnPoint(int id)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (id >= 0x1000000) {
        const size_t i = size_t(id - 0x1000000);
        if (i >= s.spawn_point_adds.size()) return;
        s.spawn_point_adds.erase(s.spawn_point_adds.begin() + i);
    } else {
        const size_t gi = size_t(id) >> 8;
        const size_t si = size_t(id) & 0xFF;
        if (gi >= s.generators.size() ||
            si >= s.generators[gi].spawn_points.size()) {
            return;
        }
        auto& sp = s.generators[gi].spawn_points;
        sp.erase(sp.begin() + si);
    }
    s.dirty = true;
    ++s.revision;
}
