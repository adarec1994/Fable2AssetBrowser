void SetChestContents(uint32_t entity_hash,
                      const std::vector<uint32_t>& item_hashes)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (!s.available) return;
    s.contents_edits[entity_hash] = item_hashes;
    s.dirty = true;
    ++s.revision;
}

bool GetChestContents(uint32_t entity_hash, std::vector<uint32_t>& out)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    auto it = s.contents_edits.find(entity_hash);
    if (it == s.contents_edits.end()) return false;
    out = it->second;
    return true;
}

void ClearChestContents(uint32_t entity_hash)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    s.contents_edits.erase(entity_hash);
    ++s.revision;
}

size_t ChestContentsEditCount()
{
    std::lock_guard<std::mutex> lk(mtx());
    return st().contents_edits.size();
}

bool GetContainerLootTable(uint32_t entity_hash, uint32_t& out)
{
    std::lock_guard<std::mutex> lk(mtx());
    const auto& edits = st().contents_loot_edits;
    const auto it = edits.find(entity_hash);
    if (it == edits.end()) return false;
    out = it->second;
    return true;
}

void SetContainerLootTable(uint32_t entity_hash, uint32_t loot_record)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (!s.available) return;
    s.contents_loot_edits[entity_hash] = loot_record;
    s.dirty = true;
    ++s.revision;
}

void ClearContainerLootTable(uint32_t entity_hash)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    s.contents_loot_edits.erase(entity_hash);
    ++s.revision;
}

bool AdditionIsChest(int index)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    return index >= 0 && size_t(index) < s.additions.size() &&
           s.additions[size_t(index)].entity_kind ==
               AdditionEntityKind::Chest;
}

uint32_t GetAdditionLootTable(int index)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (index < 0 || size_t(index) >= s.additions.size()) return 0;
    return s.additions[size_t(index)].loot_table_record;
}


void SetAdditionLootTable(int index, uint32_t loot_record)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (index < 0 || size_t(index) >= s.additions.size()) return;
    s.additions[size_t(index)].loot_table_record = loot_record;
    s.dirty = true;
    ++s.revision;
}

bool GetAdditionChestItems(int index, std::vector<uint32_t>& out)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (index < 0 || size_t(index) >= s.additions.size() ||
        s.additions[size_t(index)].entity_kind !=
            AdditionEntityKind::Chest) {
        return false;
    }
    out = s.additions[size_t(index)].chest_items;
    return true;
}

void SetAdditionChestItems(int index, const std::vector<uint32_t>& items)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (index < 0 || size_t(index) >= s.additions.size()) return;
    s.additions[size_t(index)].chest_items = items;
    s.additions[size_t(index)].entity_kind = AdditionEntityKind::Chest;
    s.dirty = true;
    ++s.revision;
}

void MarkAdditionEntityKind(int index, AdditionEntityKind kind)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (index < 0 || size_t(index) >= s.additions.size()) return;
    auto& a = s.additions[size_t(index)];
    a.entity_kind = kind;
    if (kind != AdditionEntityKind::Chest) {
        a.silver_keys_needed = 0;
        a.is_dig_spot = false;
    }
    s.dirty = true;
    ++s.revision;
}

void MarkAdditionAsSilverKeyChest(int index, int silver_keys_needed)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (index < 0 || size_t(index) >= s.additions.size()) return;
    auto& a = s.additions[size_t(index)];
    a.entity_kind = AdditionEntityKind::Chest;
    a.is_dig_spot = false;
    a.silver_keys_needed = std::max(1, silver_keys_needed);
    s.dirty = true;
    ++s.revision;
}

void MarkAdditionAsContainer(int index,
                             const ContainerTemplateInfo& info)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (index < 0 || size_t(index) >= s.additions.size()) return;
    auto& a = s.additions[size_t(index)];
    a.entity_kind = AdditionEntityKind::Chest;
    a.entity_name = info.entity_name;
    a.is_dig_spot = info.is_dig_spot;
    a.silver_keys_needed = info.silver_keys_needed;
    a.entity_template = info.entity_template;
    a.entity_comp_field = info.transform_component_field;
    a.entity_comp_template = info.transform_component_template;
    a.physics_file_hash = info.physics_file_hash;
    a.chest_items = info.initial_items;
    a.loot_table_record = info.potential_items_record;
    s.dirty = true;
    ++s.revision;
}

int AddContainerPlacement(const std::string& model_path,
                          const float pos[3],
                          const ContainerTemplateInfo& info)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (!s.available || s.saving || !pos || !info.entity_template ||
        !info.transform_component_field) {
        return -1;
    }
    Addition a;
    a.model_path = model_path;
    a.entity_name = info.entity_name;
    a.pos[0] = pos[0];
    a.pos[1] = pos[1];
    a.pos[2] = pos[2];
    a.entity_kind = AdditionEntityKind::Chest;
    a.is_dig_spot = info.is_dig_spot;
    a.silver_keys_needed = info.silver_keys_needed;
    a.entity_template = info.entity_template;
    a.entity_comp_field = info.transform_component_field;
    a.entity_comp_template = info.transform_component_template;
    a.physics_file_hash = info.physics_file_hash;
    a.chest_items = info.initial_items;
    a.loot_table_record = info.potential_items_record;
    s.additions.push_back(std::move(a));
    s.dirty = true;
    ++s.revision;
    return int(s.additions.size()) - 1;
}

bool AdditionIsDigSpot(int index)
{
    std::lock_guard<std::mutex> lk(mtx());
    const auto& s = st();
    return index >= 0 && size_t(index) < s.additions.size() &&
           s.additions[size_t(index)].entity_kind ==
               AdditionEntityKind::Chest &&
           s.additions[size_t(index)].is_dig_spot;
}

int AddNpcPlacement(const float pos[3], const NpcPlacementInfo& info)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (!s.available || s.saving || !pos || !info.valid()) return -1;
    Addition a;
    a.entity_kind = AdditionEntityKind::Npc;
    a.entity_name = info.instance_name;
    a.creature_name = info.creature_name;
    a.entity_template = info.creature_entity;
    a.entity_comp_field = info.transform_component_field;
    a.entity_comp_template = info.transform_component_template;
    a.entity_position_template = info.position_template;
    a.entity_rotation_template = info.rotation_template;
    a.asset_models = info.asset_models;
    if (!a.asset_models.empty()) a.model_path = a.asset_models.front();
    a.pos[0] = pos[0];
    a.pos[1] = pos[1];
    a.pos[2] = pos[2];
    s.additions.push_back(std::move(a));
    s.dirty = true;
    ++s.revision;
    return int(s.additions.size()) - 1;
}

bool AdditionIsNpc(int index)
{
    std::lock_guard<std::mutex> lk(mtx());
    const auto& s = st();
    return index >= 0 && size_t(index) < s.additions.size() &&
           s.additions[size_t(index)].entity_kind ==
               AdditionEntityKind::Npc;
}

bool AdditionIsNamedEntity(int index)
{
    std::lock_guard<std::mutex> lk(mtx());
    const auto& s = st();
    if (index < 0 || size_t(index) >= s.additions.size()) return false;
    const Addition& a = s.additions[size_t(index)];
    return !a.entity_name.empty() &&
           (a.entity_kind == AdditionEntityKind::Npc ||
            a.entity_kind == AdditionEntityKind::GenericProp);
}

void MarkAdditionAsPropEntity(int index,
                              uint32_t template_hash,
                              uint32_t comp_field_hash,
                              uint32_t comp_template_hash,
                              uint32_t physics_file_hash,
                              bool has_text_tags)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (index < 0 || size_t(index) >= s.additions.size()) return;
    auto& a = s.additions[size_t(index)];
    a.entity_kind = AdditionEntityKind::GenericProp;
    a.is_dig_spot = false;
    a.entity_template = template_hash;
    a.entity_comp_field = comp_field_hash;
    a.entity_comp_template = comp_template_hash;
    a.physics_file_hash = physics_file_hash;
    a.entity_has_text = has_text_tags;
    s.dirty = true;
    ++s.revision;
}

void MarkAdditionAsStaticProp(int index,
                              const StaticPropPlacementInfo& info)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (index < 0 || size_t(index) >= s.additions.size() ||
        !info.valid()) {
        return;
    }
    Addition& a = s.additions[size_t(index)];
    a.entity_kind = AdditionEntityKind::GenericProp;
    a.entity_name = info.instance_name;
    a.is_dig_spot = false;
    a.entity_template = info.entity_template;
    a.entity_comp_field = info.transform_component_field;
    a.entity_comp_template = info.transform_component_template;
    a.entity_position_template = info.position_template;
    a.entity_rotation_template = info.rotation_template;
    a.physics_file_hash = 0;
    a.entity_has_text = false;
    s.dirty = true;
    ++s.revision;
}

bool AdditionIsReadable(int index)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    return index >= 0 && size_t(index) < s.additions.size() &&
           s.additions[size_t(index)].entity_has_text;
}

bool GetAdditionReadableText(int index, std::string& out)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (index < 0 || size_t(index) >= s.additions.size() ||
        !s.additions[size_t(index)].entity_has_text) {
        return false;
    }
    out = s.additions[size_t(index)].readable_text;
    return true;
}

void SetAdditionReadableText(int index, const std::string& text)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (index < 0 || size_t(index) >= s.additions.size()) return;
    s.additions[size_t(index)].readable_text = text;
    s.dirty = true;
    ++s.revision;
}

void SetEntityTextEdit(uint32_t tag_hash, const std::string& utf8)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    if (!s.available || tag_hash == 0) return;
    s.text_edits[tag_hash] = utf8;
    s.dirty = true;
    ++s.revision;
}

bool GetEntityTextEdit(uint32_t tag_hash, std::string& out)
{
    std::lock_guard<std::mutex> lk(mtx());
    auto& s = st();
    auto it = s.text_edits.find(tag_hash);
    if (it == s.text_edits.end()) return false;
    out = it->second;
    return true;
}

size_t TextEditCount()
{
    std::lock_guard<std::mutex> lk(mtx());
    return st().text_edits.size();
}
