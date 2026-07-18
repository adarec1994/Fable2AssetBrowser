#ifdef _WIN32
static void draw_gdb_placements_overlay(const ImVec2& origin,
                                        const ImVec2& region)
{
    using namespace DirectX;
    if (g_level_gdb_placements.empty()) return;
    if (!S.show_gdb_placements) return;

    float cy = cosf(g_flycam.yaw);
    float sy = sinf(g_flycam.yaw);
    float cp = cosf(g_flycam.pitch);
    float sp = sinf(g_flycam.pitch);
    XMVECTOR eye = XMVectorSet(g_flycam.pos[0], g_flycam.pos[1], g_flycam.pos[2], 1);
    XMVECTOR at  = XMVectorSet(g_flycam.pos[0] + sy * cp,
                               g_flycam.pos[1] + sp,
                               g_flycam.pos[2] + cy * cp, 1);
    XMVECTOR up  = XMVectorSet(0, 1, 0, 0);
    XMMATRIX V = XMMatrixLookAtLH(eye, at, up);
    float fov = XMConvertToRadians(60.0f);
    float aspect = region.x / std::max(1.0f, region.y);
    float far_plane = std::max(g_mp.radius * 100.0f, 1000.0f);
    XMMATRIX P = XMMatrixPerspectiveFovLH(fov, aspect, 0.05f, far_plane);
    XMMATRIX VP = V * P;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 col_fixed = IM_COL32(255, 80, 80, 230);
    const ImU32 col_var   = IM_COL32(120, 220, 255, 180);

    const int   gw = g_pending_terrain_ghf_width;
    const int   gh = g_pending_terrain_ghf_height;
    const float tile = g_pending_terrain_ghf_tile_size > 0.0f
                         ? g_pending_terrain_ghf_tile_size : 0.5f;
    const auto& heights = g_pending_terrain_ghf_heights;
    const bool  have_terrain = (gw > 0 && gh > 0 &&
                                heights.size() == size_t(gw) * size_t(gh));
    auto sample_height = [&](float wx, float wy) -> float {
        if (!have_terrain) return 0.0f;
        const float gx = wx / tile;
        const float gy = wy / tile;
        int ix = int(gx); int iy = int(gy);
        if (ix < 0) ix = 0; else if (ix >= gw) ix = gw - 1;
        if (iy < 0) iy = 0; else if (iy >= gh) iy = gh - 1;
        return heights[size_t(iy) * size_t(gw) + size_t(ix)];
    };

    size_t drawn = 0;
    for (const auto& gp : g_level_gdb_placements) {
        const float rx = gp.x;
        const float ry = sample_height(gp.x, gp.y) + 1.0f;
        const float rz = gp.y;
        XMVECTOR clip = XMVector4Transform(XMVectorSet(rx, ry, rz, 1.0f), VP);
        float w = XMVectorGetW(clip);
        if (w <= 0.05f) continue;
        float ndcx = XMVectorGetX(clip) / w;
        float ndcy = XMVectorGetY(clip) / w;
        if (ndcx < -1.2f || ndcx > 1.2f) continue;
        if (ndcy < -1.2f || ndcy > 1.2f) continue;
        ImVec2 sp_screen;
        sp_screen.x = origin.x + (ndcx * 0.5f + 0.5f) * region.x;
        sp_screen.y = origin.y + (1.0f - (ndcy * 0.5f + 0.5f)) * region.y;

        const bool fixed = (gp.marker == 0x00004B40);
        const ImU32 col  = fixed ? col_fixed : col_var;
        const float r    = fixed ? 4.0f : 2.5f;
        dl->AddCircleFilled(sp_screen, r, col);
        if (fixed) {
            dl->AddCircle(sp_screen, r + 1.0f, IM_COL32(0, 0, 0, 200), 12, 1.0f);
        }
        if (fixed) {
            dl->AddText(ImVec2(sp_screen.x + r + 4.0f, sp_screen.y - 7.0f),
                        IM_COL32(255, 150, 150, 235), "Player start");
        }
        ++drawn;
    }

    char buf[96];
    std::snprintf(buf, sizeof(buf),
                  "gdb placements: %zu shown / %zu total",
                  drawn, g_level_gdb_placements.size());
    dl->AddText(ImVec2(origin.x + 14, origin.y + region.y - 22),
                IM_COL32(220, 220, 220, 200), buf);
}

static int g_sel_spawn_marker = -1;
static int g_sel_pending_sp = -1;
static int g_sel_pending_gen = -1;
static bool g_marker_clear_selection = false;
static bool g_add_menu_requested = false;
static float g_add_menu_requested_pos[3] = {0, 0, 0};
static size_t g_player_start_placement = std::numeric_limits<size_t>::max();

static bool level_marker_visible(const LevelSpawnMarker& marker)
{
    if (::is_player_start_marker(marker)) return true;
    if (marker.kind == 0) return false;
    if (marker.kind == 4) return S.show_dig_spots;
    if (marker.is_container && S.show_containers) return true;
    if (marker.kind == 3) return S.show_ent_npcs;
    if (marker.kind == 6) return S.show_entity_models;
    if (marker.kind == 5) return false;
    return S.show_spawn_markers;
}

static std::string pretty_container_item_tag(std::string tag, int money)
{
    for (const char* pfx : {"INV_ITEM_", "OBJECT_", "TEXT_"}) {
        const size_t n = std::strlen(pfx);
        if (tag.size() > n && tag.compare(0, n, pfx) == 0) {
            tag = tag.substr(n);
            break;
        }
    }
    if (tag.size() > 5 &&
        tag.compare(tag.size() - 5, 5, "_NAME") == 0) {
        tag.resize(tag.size() - 5);
    }
    if (tag.find('_') != std::string::npos ||
        std::none_of(tag.begin(), tag.end(), [](unsigned char c) {
            return std::islower(c);
        })) {
        bool word_start = true;
        for (char& c : tag) {
            if (c == '_') {
                c = ' ';
                word_start = true;
            } else {
                c = word_start
                    ? char(std::toupper(static_cast<unsigned char>(c)))
                    : char(std::tolower(static_cast<unsigned char>(c)));
                word_start = false;
            }
        }
    }
    if (money >= 0) {
        tag += " (" + std::to_string(money) + " gold)";
    }
    return tag;
}

static std::string container_catalog_label(uint32_t record_hash)
{
    for (const auto& item : g_item_details) {
        if (item.record_hash == record_hash && !item.display_name.empty()) {
            return item.display_name;
        }
    }
    for (const auto& item : g_level_item_catalog) {
        if (item.record_hash == record_hash) {
            return pretty_container_item_tag(item.label, item.money);
        }
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08X", record_hash);
    return buf;
}

static std::string container_item_label(const Gdb::EntityContentsItem& item)
{
    if (!item.display_name.empty()) return item.display_name;
    std::string tag = !item.name_tag.empty() ? item.name_tag
                                             : item.entry_label;
    if (tag.empty()) return container_catalog_label(item.record_hash);
    return pretty_container_item_tag(std::move(tag), item.money);
}

static const char* container_yes_no_unknown(int value)
{
    if (value < 0) return "Not authored";
    return value != 0 ? "Yes" : "No";
}

static void draw_container_authored_rules(
    const Gdb::EntityContents& contents)
{
    const char* kind = "Inventory container";
    if (contents.is_dig_spot) kind = "Dig spot";
    else if (contents.has_chest_component) kind = "Chest";
    ImGui::TextColored(ImVec4(0.55f, 0.9f, 1.0f, 1.0f), "%s", kind);

    if (contents.has_chest_component &&
        contents.silver_keys_needed > 0) {
        ImGui::Text("Silver keys required: %d",
                    contents.silver_keys_needed);
    }

    if (contents.item_repopulation_record != 0) {
        ImGui::Text("Can be stolen from: %s",
                    container_yes_no_unknown(
                        contents.can_be_stolen_from));
        const char* mode = "Never";
        if (contents.can_respawn_items_repeatedly > 0) {
            mode = "Repeatedly";
        } else if (contents.can_respawn_items_once > 0) {
            mode = "Once";
        } else if (contents.can_respawn_items_repeatedly < 0 &&
                   contents.can_respawn_items_once < 0) {
            mode = "Not authored";
        }
        ImGui::Text("Loot repopulation: %s", mode);
        if (contents.chance_of_respawning >= 0.0f) {
            ImGui::Text("Authored repopulation chance: %.1f%%",
                        contents.chance_of_respawning);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "The game applies its global item-respawn multiplier "
                    "and any active context multiplier at runtime.");
            }
        }
    } else {
        ImGui::Text("Loot repopulation: Not configured");
    }

    if (contents.is_dig_spot) {
        ImGui::Text("Dog can lead to: %s",
                    contents.dog_can_lead_to ? "Yes" : "No");
        if (contents.dog_lead_radius >= 0.0f) {
            ImGui::Text("Dog search radius: %.1f",
                        contents.dog_lead_radius);
        }
        if (contents.dog_lead_priority >= 0) {
            ImGui::Text("Dog lead priority: %d",
                        contents.dog_lead_priority);
        }
    }

}

static void draw_container_potential_items(
    const Gdb::EntityContents& contents,
    size_t max_rows = 12)
{
    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                       "Potential items:");
    if (contents.potential_items.empty()) {
        ImGui::TextDisabled("  (none)");
        return;
    }

    const bool scroll = contents.potential_items.size() > max_rows;
    if (scroll) {
        ImGui::BeginChild("##potential_items_scroll",
                          ImVec2(0.0f, 240.0f), true);
    }
    const size_t count = contents.potential_items.size();
    for (size_t i = 0; i < count; ++i) {
        const auto& item = contents.potential_items[i];
        ImGui::PushID(int(i) + 0x5A00);
        ImGui::BulletText("%s", container_item_label(item).c_str());
        ImGui::PopID();
    }
    if (scroll) ImGui::EndChild();
}



static void draw_dig_spot_level_selector(uint32_t current_table,
                                         bool editable,
                                         bool is_addition,
                                         uint32_t entity_hash,
                                         int addition_index)
{
    struct LevelRow {
        int level = 0;
        uint32_t table = 0;
    };
    std::vector<LevelRow> rows;
    constexpr char kPrefix[] = "MarkerDiggingSpotLevel";
    constexpr size_t kPrefixLen = sizeof(kPrefix) - 1;
    for (const auto& [hash, c] : g_level_entity_contents) {
        (void)hash;
        if (!c.potential_items_record) continue;
        if (c.entity_name.rfind(kPrefix, 0) != 0) continue;
        const int level = std::atoi(c.entity_name.c_str() + kPrefixLen);
        if (level <= 0) continue;
        bool duplicate = false;
        for (const LevelRow& row : rows) {
            if (row.level == level) { duplicate = true; break; }
        }
        if (!duplicate) rows.push_back({level, c.potential_items_record});
    }
    if (rows.empty()) return;
    std::sort(rows.begin(), rows.end(),
              [](const LevelRow& a, const LevelRow& b) {
                  return a.level < b.level;
              });

    std::string current_label = "-";
    for (const LevelRow& row : rows) {
        if (row.table == current_table) {
            current_label = "Level " + std::to_string(row.level);
            break;
        }
    }

    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                       "Dig spot level:");
    ImGui::BeginDisabled(!editable);
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::BeginCombo("##dig_spot_level", current_label.c_str())) {
        for (const LevelRow& row : rows) {
            const std::string label = "Level " + std::to_string(row.level);
            if (ImGui::Selectable(label.c_str(),
                                  row.table == current_table)) {
                if (is_addition) {
                    LevelEdit::SetAdditionLootTable(addition_index,
                                                    row.table);
                } else {
                    LevelEdit::SetContainerLootTable(entity_hash,
                                                     row.table);
                }
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
}

static void draw_container_loot_table_editor(
    uint32_t entity_hash,
    const Gdb::EntityContents& contents)
{
    uint32_t current = contents.potential_items_record;
    const bool staged =
        LevelEdit::GetContainerLootTable(entity_hash, current);
    std::string current_label = "None";
    const Gdb::EntityContents* current_source = nullptr;
    if (current != 0) {
        for (const auto& [hash, candidate] : g_level_entity_contents) {
            (void)hash;
            if (candidate.potential_items_record != current) continue;
            current_source = &candidate;
            current_label = candidate.entity_name.empty()
                ? "Authored loot table" : candidate.entity_name;
            break;
        }
        if (!current_source) current_label = "Unknown authored table";
    }

    const bool editable = LevelEdit::Enabled() && !LevelEdit::Saving();
    if (contents.is_dig_spot) {
        draw_dig_spot_level_selector(current, editable, false,
                                     entity_hash, -1);
    }
    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                       staged ? "Random loot (edited):" : "Random loot:");
    ImGui::BeginDisabled(!editable);
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::BeginCombo("##container_random_loot",
                          current_label.c_str())) {
        if (ImGui::Selectable("None", current == 0)) {
            LevelEdit::SetContainerLootTable(entity_hash, 0);
        }
        std::unordered_set<uint32_t> seen;
        for (const auto& [hash, candidate] : g_level_entity_contents) {
            (void)hash;
            if (!candidate.potential_items_record ||
                !seen.insert(candidate.potential_items_record).second) {
                continue;
            }
            std::string label = candidate.entity_name.empty()
                ? "Authored loot table" : candidate.entity_name;
            label += "##" + std::to_string(
                candidate.potential_items_record);
            if (ImGui::Selectable(
                    label.c_str(),
                    current == candidate.potential_items_record)) {
                LevelEdit::SetContainerLootTable(
                    entity_hash, candidate.potential_items_record);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    if (staged && editable) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Revert##container_loot")) {
            LevelEdit::ClearContainerLootTable(entity_hash);
        }
    }
    if (current_source) {
        draw_container_potential_items(*current_source);
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                           "Potential items:");
        ImGui::TextDisabled("  (none)");
    }
}

static void draw_entity_gameplay_details(
    const Gdb::EntityGameplayDetails& details,
    bool show_entity_name)
{
    ImGui::Spacing();
    ImGui::Separator();
    if (show_entity_name && !details.entity_name.empty()) {
        ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f), "%s",
                           details.entity_name.c_str());
    }
    ImGui::TextColored(ImVec4(0.55f, 0.9f, 1.0f, 1.0f),
                       "Entity gameplay");
    if (!details.faction_name.empty()) {
        ImGui::Text("Faction / allegiance: %s",
                    details.faction_name.c_str());
    }
    if (!details.combat_profile_name.empty()) {
        ImGui::TextWrapped("Combat profile: %s",
                           details.combat_profile_name.c_str());
    }

    if (!details.core_fields.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                           "Core stats:");
        for (const auto& field : details.core_fields) {
            ImGui::Text("%s: %s", field.label.c_str(),
                        field.value.c_str());
        }
    }
    if (!details.combat_fields.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                           "Combat:");
        for (const auto& field : details.combat_fields) {
            ImGui::Text("%s: %s", field.label.c_str(),
                        field.value.c_str());
        }
    }

}

static void draw_property_details(const Gdb::PropertyDetails& details)
{
    ImGui::Spacing();
    ImGui::Separator();
    const std::string& title = !details.display_name.empty()
        ? details.display_name : details.building_entity_name;
    if (!title.empty()) {
        ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.55f, 1.0f), "%s",
                           title.c_str());
    }
    ImGui::TextColored(ImVec4(0.55f, 0.9f, 1.0f, 1.0f),
                       "Property");
    if (!details.building_entity_name.empty() &&
        details.building_entity_name != title) {
        ImGui::TextWrapped("Level instance: %s",
                           details.building_entity_name.c_str());
    }
    if (!details.building_type_name.empty()) {
        ImGui::Text("Type: %s", details.building_type_name.c_str());
    }
    if (!details.address.empty()) {
        ImGui::TextWrapped("Address: %s", details.address.c_str());
    }
    if (details.basic_sale_price >= 0) {
        ImGui::Text("Base sale price: %d gold",
                    details.basic_sale_price);
    }
    if (details.can_rent >= 0) {
        ImGui::Text("Can rent: %s", details.can_rent ? "Yes" : "No");
    }
    if (!details.has_building_record) {
        ImGui::TextDisabled(
            "The linked building data is not loaded in this scenario.");
        return;
    }
    const bool has_more =
        (!details.benefits.empty() &&
         details.benefits != "BUILDING_BENEFITS_NONE") ||
        (!details.anecdotes.empty() &&
         details.anecdotes != "BUILDING_ANECDOTES_NONE") ||
        !details.fields.empty();
    if (has_more) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.55f, 0.9f, 1.0f, 1.0f),
                           "Property Data:");
        if (!details.benefits.empty() &&
            details.benefits != "BUILDING_BENEFITS_NONE") {
            ImGui::TextWrapped("Benefits: %s", details.benefits.c_str());
        }
        if (!details.anecdotes.empty() &&
            details.anecdotes != "BUILDING_ANECDOTES_NONE") {
            ImGui::TextWrapped("Details: %s", details.anecdotes.c_str());
        }
        for (const auto& field : details.fields) {
            ImGui::TextWrapped("%s: %s", field.label.c_str(),
                               field.value.c_str());
        }
    }
}

static void draw_level_container_details(uint32_t entity_hash)
{
    auto found = g_level_entity_contents.find(entity_hash);
    if (found == g_level_entity_contents.end()) return;
    const Gdb::EntityContents& contents = found->second;

    ImGui::Separator();
    draw_container_authored_rules(contents);

    std::vector<uint32_t> staged_items;
    const bool staged =
        LevelEdit::GetChestContents(entity_hash, staged_items);
    if (staged || !contents.initial_items.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                           staged ? "Initial items (edited):"
                                  : "Initial items:");
        const size_t item_count = staged ? staged_items.size()
                                         : contents.initial_items.size();
        for (size_t i = 0; i < item_count; ++i) {
            const std::string label = staged
                ? container_catalog_label(staged_items[i])
                : container_item_label(contents.initial_items[i]);
            ImGui::BulletText("%s", label.c_str());
        }
        if (item_count == 0) ImGui::TextDisabled("  (empty)");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                           "Initial items:");
        ImGui::TextDisabled("  (none)");
    }
    draw_container_loot_table_editor(entity_hash, contents);
}

static void draw_addition_container_details(int addition_index)
{
    const bool editable = LevelEdit::Enabled() && !LevelEdit::Saving();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f), "%s",
                       LevelEdit::AdditionIsDigSpot(addition_index)
                           ? "New dig spot (unsaved)"
                           : "New container (unsaved)");
    std::vector<uint32_t> items;
    LevelEdit::GetAdditionChestItems(addition_index, items);
    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                       "Initial items:");
    int remove_index = -1;
    for (size_t i = 0; i < items.size(); ++i) {
        ImGui::PushID(int(i) + 0x7300);
        if (editable) {
            if (ImGui::SmallButton("x")) remove_index = int(i);
            ImGui::SameLine();
        } else {
            ImGui::Bullet();
            ImGui::SameLine();
        }
        ImGui::TextUnformatted(container_catalog_label(items[i]).c_str());
        ImGui::PopID();
    }
    if (items.empty()) ImGui::TextDisabled("  (empty)");
    if (remove_index >= 0 && size_t(remove_index) < items.size()) {
        items.erase(items.begin() + remove_index);
        LevelEdit::SetAdditionChestItems(addition_index, items);
    }

    static int s_addition_picker = -1;
    static char s_addition_filter[64] = {};
    if (editable && ImGui::SmallButton("+ Add item")) {
        s_addition_picker = addition_index;
        s_addition_filter[0] = 0;
        ImGui::OpenPopup("##marker_container_item_picker");
    }
    if (ImGui::BeginPopup("##marker_container_item_picker")) {
        if (s_addition_picker != addition_index) {
            ImGui::CloseCurrentPopup();
        } else {
            ImGui::SetNextItemWidth(260.0f);
            ImGui::InputTextWithHint("##marker_item_filter",
                                     "Search items...",
                                     s_addition_filter,
                                     sizeof(s_addition_filter));
            std::string filter = s_addition_filter;
            std::transform(filter.begin(), filter.end(), filter.begin(),
                           ::tolower);
            ImGui::BeginChild("##marker_item_rows",
                              ImVec2(300.0f, 260.0f), true);
            for (const auto& item : g_item_details) {
                const std::string label = item.display_name.empty()
                    ? item.label : item.display_name;
                std::string low = label;
                std::transform(low.begin(), low.end(), low.begin(),
                               ::tolower);
                if (!filter.empty() &&
                    low.find(filter) == std::string::npos) {
                    continue;
                }
                ImGui::PushID(int(item.record_hash));
                if (ImGui::Selectable(label.c_str())) {
                    items.push_back(item.record_hash);
                    LevelEdit::SetAdditionChestItems(addition_index, items);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
        }
        ImGui::EndPopup();
    }

    const uint32_t current =
        LevelEdit::GetAdditionLootTable(addition_index);
    if (LevelEdit::AdditionIsDigSpot(addition_index)) {
        draw_dig_spot_level_selector(current, editable, true, 0,
                                     addition_index);
    }
    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                       "Random loot:");
    std::string current_label = "None";
    for (const auto& [hash, contents] : g_level_entity_contents) {
        (void)hash;
        if (contents.potential_items_record == current && current != 0) {
            current_label = contents.entity_name.empty()
                ? "Authored loot table" : contents.entity_name;
            break;
        }
    }
    ImGui::BeginDisabled(!editable);
    if (ImGui::BeginCombo("##marker_random_loot", current_label.c_str())) {
        if (ImGui::Selectable("None", current == 0)) {
            LevelEdit::SetAdditionLootTable(addition_index, 0);
        }
        std::unordered_set<uint32_t> seen_tables;
        for (const auto& [hash, contents] : g_level_entity_contents) {
            (void)hash;
            if (!contents.potential_items_record ||
                !seen_tables.insert(contents.potential_items_record).second) {
                continue;
            }
            const std::string label = contents.entity_name.empty()
                ? "Authored loot table" : contents.entity_name;
            if (ImGui::Selectable(
                    label.c_str(), current == contents.potential_items_record)) {
                LevelEdit::SetAdditionLootTable(
                    addition_index, contents.potential_items_record);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("authored into the level GDB on Save");
}

struct ContainerSpawnChoice {
    std::string label;
    std::string model_path;
    bool is_dive = false;
    LevelEdit::ContainerTemplateInfo info;
};

static uint32_t level_model_path_hash(const std::string& path)
{
    uint32_t hash = 0x811C9DC5u;
    for (unsigned char c : path) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<unsigned char>(c - 'A' + 'a');
        }
        if (c == '/') c = '\\';
        hash *= 0x01000193u;
        hash ^= uint32_t(c);
    }
    return hash;
}

static std::string container_spawn_label(std::string value)
{
    const size_t slash = value.find_last_of("/\\");
    if (slash != std::string::npos) value.erase(0, slash + 1);
    const size_t dot = value.rfind('.');
    if (dot != std::string::npos) value.resize(dot);
    for (char& c : value) {
        if (c == '_' || c == '-') c = ' ';
    }
    if (value.empty()) value = "Unnamed container";
    return value;
}

static std::vector<ContainerSpawnChoice> build_container_spawn_choices()
{
    static std::string cached_level;
    static size_t cached_contents = size_t(-1);
    static size_t cached_models = size_t(-1);
    static std::vector<ContainerSpawnChoice> cached;
    if (cached_level == g_pending_terrain_label &&
        cached_contents == g_level_entity_contents.size() &&
        cached_models == S.all_mdl_files.size()) {
        return cached;
    }
    std::unordered_map<uint32_t, std::string> models_by_hash;
    models_by_hash.reserve(S.all_mdl_files.size() * 2);
    for (const auto& model : S.all_mdl_files) {
        models_by_hash.emplace(level_model_path_hash(model.full_path),
                               model.full_path);
    }
    std::unordered_map<uint32_t, uint32_t> placement_models;
    for (const auto& placement : g_level_gdb_placements) {
        if (placement.hash && placement.model_path_hash) {
            placement_models.emplace(placement.hash,
                                     placement.model_path_hash);
        }
    }

    std::vector<ContainerSpawnChoice> out;
    std::unordered_set<std::string> seen;
    for (const auto& [entity_hash, contents] : g_level_entity_contents) {
        if (!contents.is_dig_spot && !contents.is_dive_spot &&
            g_level_entity_gameplay.count(entity_hash)) {
            continue;
        }
        if (!contents.is_dig_spot && !contents.is_dive_spot &&
            !contents.has_inventory_component &&
            !contents.has_chest_component) {
            continue;
        }

        uint32_t model_hash = contents.model_path_hash;
        if (!model_hash) {
            const auto placed = placement_models.find(entity_hash);
            if (placed != placement_models.end()) model_hash = placed->second;
        }
        ContainerSpawnChoice choice;
        choice.is_dive = contents.is_dive_spot;
        choice.info.entity_name = contents.entity_name;
        choice.info.is_dig_spot = contents.is_dig_spot;
        choice.info.silver_keys_needed =
            std::max(0, contents.silver_keys_needed);
        choice.info.entity_template = contents.entity_template;
        choice.info.transform_component_field =
            contents.transform_component_field;
        choice.info.transform_component_template =
            contents.transform_component_template;
        choice.info.physics_file_hash = contents.physics_file_hash;
        choice.info.potential_items_record =
            contents.potential_items_record;
        for (const auto& item : contents.initial_items) {
            choice.info.initial_items.push_back(item.record_hash);
        }
        if (model_hash) {
            const auto model = models_by_hash.find(model_hash);
            if (model != models_by_hash.end()) {
                choice.model_path = model->second;
            }
            const auto prop =
                g_level_prop_entity_templates.find(model_hash);
            if (prop != g_level_prop_entity_templates.end()) {
                const auto& donor = prop->second;
                if (!choice.info.entity_template) {
                    choice.info.entity_template = donor.template_hash;
                }
                if (!choice.info.transform_component_field) {
                    choice.info.transform_component_field =
                        donor.comp_field_hash;
                }
                if (!choice.info.transform_component_template) {
                    choice.info.transform_component_template =
                        donor.comp_template_hash;
                }
                if (!choice.info.physics_file_hash) {
                    choice.info.physics_file_hash = donor.physics_file_hash;
                }
            }
        }
        if (!choice.info.entity_template ||
            !choice.info.transform_component_field) {
            continue;
        }
        choice.label = container_spawn_label(
            !choice.model_path.empty() ? choice.model_path
                                       : choice.info.entity_name);
        char key[96];
        std::snprintf(key, sizeof(key), "%d:%08X:%08X",
                      choice.info.is_dig_spot ? 1 : 0,
                      choice.info.entity_template, model_hash);
        if (!seen.insert(key).second) continue;
        out.push_back(std::move(choice));
    }
    std::sort(out.begin(), out.end(),
              [](const auto& a, const auto& b) {
                  if (a.info.is_dig_spot != b.info.is_dig_spot) {
                      return !a.info.is_dig_spot;
                  }
                  return a.label < b.label;
              });
    cached_level = g_pending_terrain_label;
    cached_contents = g_level_entity_contents.size();
    cached_models = S.all_mdl_files.size();
    cached = out;
    return cached;
}

static std::string quest_level_id_from_path(std::string path)
{
    std::replace(path.begin(), path.end(), '/', '\\');
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    const std::string root = "worlds\\albion\\";
    const size_t root_pos = lower.find(root);
    if (root_pos != std::string::npos) {
        path.erase(0, root_pos + root.size());
        lower.erase(0, root_pos + root.size());
    }
    const std::string suffix =
        "\\defaultscenario\\defaultscenario.engine_level";
    const size_t suffix_pos = lower.rfind(suffix);
    if (suffix_pos != std::string::npos) path.resize(suffix_pos);
    if (path.empty()) path = g_pending_terrain_label;
    return path;
}

static bool build_quest_level_reference(
    const LevelSpawnMarker& marker,
    size_t marker_index,
    QuestUI::LevelReferenceCandidate& candidate)
{
    candidate = QuestUI::LevelReferenceCandidate{};
    candidate.is_npc = marker.kind == 3;
    candidate.is_container = marker.is_container;
    candidate.level_path = g_pending_terrain_level_entry.full_path;
    candidate.level_id = quest_level_id_from_path(candidate.level_path);
    candidate.entity_name = marker.name;
    candidate.entity_hash = marker.entity_hash;
    candidate.x = marker.x;
    candidate.y = marker.y;
    candidate.z = marker.z;
    float position_delta[3] = {};
    float rotation_delta[3] = {};
    if (LevelEdit::EditFor(0x70000000u | uint32_t(marker_index),
                           position_delta, rotation_delta)) {
        candidate.x += position_delta[0];
        candidate.y += position_delta[1];
        candidate.z += position_delta[2];
    }
    candidate.model_hashes = marker.model_hashes;
    candidate.authored_instance = marker.pending_addition_index >= 0 &&
        LevelEdit::AdditionIsNamedEntity(
            marker.pending_addition_index);
    if (candidate.entity_name.empty()) {
        const auto contents =
            g_level_entity_contents.find(candidate.entity_hash);
        if (contents != g_level_entity_contents.end()) {
            candidate.entity_name = contents->second.entity_name;
        }
    }
    return !candidate.level_id.empty() &&
           !candidate.entity_name.empty() &&
           (candidate.entity_hash != 0 || candidate.authored_instance);
}

static std::string unique_static_prop_instance_name(
    const Gdb::CreatureCatalogEntry& entity)
{
    std::string base = "F2AB_Static_" + entity.name;
    for (char& c : base) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
            c = '_';
        }
    }
    auto used = [&](const std::string& candidate) {
        for (const LevelSpawnMarker& marker : g_level_spawn_markers) {
            if (marker.name == candidate) return true;
        }
        std::vector<LevelEdit::Addition> additions;
        LevelEdit::GetAdditions(additions);
        return std::any_of(
            additions.begin(), additions.end(),
            [&](const LevelEdit::Addition& addition) {
                return !addition.removed &&
                       addition.entity_name == candidate;
            });
    };
    if (!used(base)) return base;
    for (unsigned int suffix = 2; suffix < 100000; ++suffix) {
        const std::string candidate =
            base + '_' + std::to_string(suffix);
        if (!used(candidate)) return candidate;
    }
    return base + "_New";
}

static int selected_level_spawn_marker_index()
{
    if (g_sel_spawn_marker >= 0 &&
        g_sel_spawn_marker < int(g_level_spawn_markers.size())) {
        return g_sel_spawn_marker;
    }
    if ((::g_selected_level_pick_id & 0xF0000000u) == 0x70000000u) {
        const size_t marker_index =
            size_t(::g_selected_level_pick_id & 0x0FFFFFFFu);
        if (marker_index < g_level_spawn_markers.size()) {
            return int(marker_index);
        }
    }
    if (::g_selected_level_mesh_idx < 0 ||
        ::g_selected_level_mesh_idx >= int(g_mp.meshes.size()) ||
        ::g_selected_level_pick_id == 0) {
        return -1;
    }
    const MPPerMesh& mesh =
        g_mp.meshes[size_t(::g_selected_level_mesh_idx)];
    uint32_t entity_hash = 0;
    for (const auto& range : mesh.pick_ranges) {
        if (range.selection_id != ::g_selected_level_pick_id) continue;
        entity_hash = range.gdb_entity_hash;
        break;
    }
    if (entity_hash == 0) return -1;
    for (size_t marker_index = 0;
         marker_index < g_level_spawn_markers.size(); ++marker_index) {
        if (g_level_spawn_markers[marker_index].entity_hash ==
            entity_hash) {
            return int(marker_index);
        }
    }
    return -1;
}

static void add_quest_item_to_container(uint32_t entity_hash,
                                        uint32_t item_hash)
{
    if (!entity_hash || !item_hash) return;
    std::vector<uint32_t> items;
    if (!LevelEdit::GetChestContents(entity_hash, items)) {
        const auto existing = g_level_entity_contents.find(entity_hash);
        if (existing != g_level_entity_contents.end()) {
            for (const Gdb::EntityContentsItem& item :
                 existing->second.initial_items) {
                items.push_back(item.record_hash);
            }
        }
    }
    if (std::find(items.begin(), items.end(), item_hash) == items.end()) {
        items.push_back(item_hash);
        LevelEdit::SetChestContents(entity_hash, items);
    }
}

static void draw_spawn_markers_overlay(const ImVec2& origin,
                                       const ImVec2& region,
                                       bool viewport_hovered)
{
    using namespace DirectX;
    const bool any_player_start =
        std::any_of(g_level_spawn_markers.begin(),
                    g_level_spawn_markers.end(), ::is_player_start_marker);
    if (!S.show_spawn_markers && !S.show_ent_npcs &&
        !S.show_dig_spots && !S.show_containers && !S.show_ent_text &&
        !any_player_start) {
        return;
    }
    if (!g_mp.no_tilt) return;
    if (g_sel_spawn_marker >= (int)g_level_spawn_markers.size()) {
        g_sel_spawn_marker = -1;
    }
    std::unordered_set<uint32_t> generator_spawn_points_pending_removal;
    for (const auto& marker : g_level_spawn_markers) {
        if (marker.kind == 1 &&
            LevelEdit::EntityRemovalPending(marker.entity_hash)) {
            generator_spawn_points_pending_removal.insert(
                marker.spawn_point_entities.begin(),
                marker.spawn_point_entities.end());
        }
    }
    if (g_sel_spawn_marker >= 0 &&
        (LevelEdit::EntityRemovalPending(
             g_level_spawn_markers[(size_t)g_sel_spawn_marker]
                 .entity_hash) ||
         generator_spawn_points_pending_removal.count(
             g_level_spawn_markers[(size_t)g_sel_spawn_marker]
                 .entity_hash))) {
        g_sel_spawn_marker = -1;
    }

    float cy = cosf(g_flycam.yaw);
    float sy = sinf(g_flycam.yaw);
    float cp = cosf(g_flycam.pitch);
    float sp = sinf(g_flycam.pitch);
    XMVECTOR eye = XMVectorSet(g_flycam.pos[0], g_flycam.pos[1],
                               g_flycam.pos[2], 1);
    XMVECTOR at = XMVectorSet(g_flycam.pos[0] + sy * cp,
                              g_flycam.pos[1] + sp,
                              g_flycam.pos[2] + cy * cp, 1);
    XMVECTOR up = XMVectorSet(0, 1, 0, 0);
    XMMATRIX V = XMMatrixLookAtLH(eye, at, up);
    float fov = XMConvertToRadians(60.0f);
    float aspect = region.x / std::max(1.0f, region.y);
    float far_plane = std::max(g_mp.radius * 100.0f, 1000.0f);
    XMMATRIX P = XMMatrixPerspectiveFovLH(fov, aspect, 0.05f, far_plane);
    XMMATRIX VP = V * P;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 kCol[7] = {
        IM_COL32(255, 255, 255, 220),
        IM_COL32(255, 90, 90, 235),
        IM_COL32(255, 200, 80, 235),
        IM_COL32(120, 255, 140, 235),
        IM_COL32(85, 210, 255, 235),
        IM_COL32(220, 125, 255, 235),
        IM_COL32(90, 225, 225, 235),
    };

    size_t text_drawn = 0;
    for (const auto& kv : g_level_entity_text) {
        if (!S.show_ent_text) break;
        if (!kv.second.has_pos) continue;
        XMVECTOR clip = XMVector4Transform(
            XMVectorSet(kv.second.x, kv.second.z, kv.second.y, 1.0f),
            VP);
        const float w = XMVectorGetW(clip);
        if (w <= 0.05f) continue;
        const float ndcx = XMVectorGetX(clip) / w;
        const float ndcy = XMVectorGetY(clip) / w;
        if (ndcx < -1.1f || ndcx > 1.1f) continue;
        if (ndcy < -1.1f || ndcy > 1.1f) continue;
        ImVec2 pt;
        pt.x = origin.x + (ndcx * 0.5f + 0.5f) * region.x;
        pt.y = origin.y + (1.0f - (ndcy * 0.5f + 0.5f)) * region.y;
        const float r = 4.5f;
        dl->AddQuadFilled(ImVec2(pt.x, pt.y - r),
                          ImVec2(pt.x + r, pt.y),
                          ImVec2(pt.x, pt.y + r),
                          ImVec2(pt.x - r, pt.y),
                          IM_COL32(90, 170, 255, 235));
        if (w < 30.0f) {
            dl->AddText(ImVec2(pt.x + r + 3.0f, pt.y - 7.0f),
                        IM_COL32(160, 210, 255, 235), "text");
        }
        ++text_drawn;
    }
    (void)text_drawn;

    size_t drawn = 0;
    const bool can_pick = viewport_hovered && !LevelEdit::Saving() &&
                          !LevelGizmo::WantsMouse();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool context_clicked =
        ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    int click_hit = -1;
    bool overlay_click_hit = false;
    float click_best = 12.0f * 12.0f;
    uint32_t selected_model_entity_hash = 0;
    if (::g_selected_level_pick_id != 0) {
        for (const MPPerMesh& mesh : g_mp.meshes) {
            if (!mesh.is_entity_model) continue;
            for (const auto& range : mesh.pick_ranges) {
                if (range.selection_id == ::g_selected_level_pick_id) {
                    selected_model_entity_hash = range.gdb_entity_hash;
                    break;
                }
            }
            if (selected_model_entity_hash != 0) break;
        }
    }
    for (size_t mi = 0; mi < g_level_spawn_markers.size(); ++mi) {
        const auto& m = g_level_spawn_markers[mi];
        if (LevelEdit::EntityRemovalPending(m.entity_hash) ||
            generator_spawn_points_pending_removal.count(m.entity_hash)) {
            continue;
        }
        if (m.kind == 2 &&
            LevelEdit::SpawnPointRemovalPending(m.entity_hash)) {
            continue;
        }
        if (!level_marker_visible(m)) continue;
        float ex = m.x, ey = m.y, ez = m.z;
        {
            float d_pos[3], d_rot[3];
            if (LevelEdit::EditFor(0x70000000u | uint32_t(mi), d_pos,
                                   d_rot)) {
                ex += d_pos[0];
                ey += d_pos[1];
                ez += d_pos[2];
            }
        }
        XMVECTOR clip = XMVector4Transform(
            XMVectorSet(ex, ez, ey, 1.0f), VP);
        const float w = XMVectorGetW(clip);
        if (w <= 0.05f) continue;
        const float ndcx = XMVectorGetX(clip) / w;
        const float ndcy = XMVectorGetY(clip) / w;
        if (ndcx < -1.1f || ndcx > 1.1f) continue;
        if (ndcy < -1.1f || ndcy > 1.1f) continue;
        ImVec2 pt;
        pt.x = origin.x + (ndcx * 0.5f + 0.5f) * region.x;
        pt.y = origin.y + (1.0f - (ndcy * 0.5f + 0.5f)) * region.y;
        const bool player_start = ::is_player_start_marker(m);
        const ImU32 col = m.is_container && m.kind != 4
            ? kCol[5]
            : kCol[m.kind < 7 ? m.kind : 0];
        const float r = player_start ? 7.0f
                        : m.kind == 1 ? 6.0f
                                      : (m.is_container ? 5.5f : 4.5f);
        const bool model_owns_selection =
            (m.kind == 2 || m.kind == 3 || m.kind == 6) &&
            !m.model_hashes.empty();
        const bool selected = model_owns_selection
            ? (::g_selected_level_pick_id ==
                   (0x70000000u | uint32_t(mi)) ||
               (selected_model_entity_hash != 0 &&
                selected_model_entity_hash == m.entity_hash))
            : (int(mi) == g_sel_spawn_marker);
        if (player_start) {
            
            const ImU32 kFlag = IM_COL32(70, 230, 110, 245);
            ImVec2 top = pt;
            {
                XMVECTOR tclip = XMVector4Transform(
                    XMVectorSet(ex, ez + 2.2f, ey, 1.0f), VP);
                const float tw = XMVectorGetW(tclip);
                if (tw > 0.05f) {
                    const float tx = XMVectorGetX(tclip) / tw;
                    const float ty = XMVectorGetY(tclip) / tw;
                    top.x = origin.x + (tx * 0.5f + 0.5f) * region.x;
                    top.y = origin.y +
                            (1.0f - (ty * 0.5f + 0.5f)) * region.y;
                }
            }
            dl->AddCircleFilled(pt, 4.5f, kFlag);
            dl->AddCircle(pt, 5.5f,
                          selected ? IM_COL32(255, 255, 255, 255)
                                   : IM_COL32(0, 0, 0, 200),
                          0, selected ? 2.0f : 1.0f);
            dl->AddLine(pt, top, kFlag, 2.0f);
            const float fw = std::max(10.0f, (pt.y - top.y) * 0.35f);
            dl->AddTriangleFilled(
                top, ImVec2(top.x + fw, top.y + fw * 0.4f),
                ImVec2(top.x, top.y + fw * 0.8f), kFlag);
            dl->AddText(ImVec2(top.x + fw + 4.0f, top.y - 3.0f),
                        kFlag, "Player Start");
        } else {
            dl->AddQuadFilled(ImVec2(pt.x, pt.y - r),
                              ImVec2(pt.x + r, pt.y),
                              ImVec2(pt.x, pt.y + r),
                              ImVec2(pt.x - r, pt.y), col);
            dl->AddQuad(ImVec2(pt.x, pt.y - r - 1),
                        ImVec2(pt.x + r + 1, pt.y),
                        ImVec2(pt.x, pt.y + r + 1),
                        ImVec2(pt.x - r - 1, pt.y),
                        selected ? IM_COL32(255, 255, 255, 255)
                                 : IM_COL32(0, 0, 0, 200),
                        selected ? 2.0f : 1.0f);
            if ((w < 45.0f || selected) && !m.name.empty()) {
                dl->AddText(ImVec2(pt.x + r + 3.0f, pt.y - 7.0f),
                            IM_COL32(235, 235, 235, 235),
                            m.name.c_str());
            }
        }


        if (!model_owns_selection && can_pick &&
            (clicked || context_clicked)) {
            const float dx = mouse.x - pt.x;
            const float dy = mouse.y - pt.y;
            const float d2 = dx * dx + dy * dy;
            if (d2 < click_best) {
                click_best = d2;
                click_hit = int(mi);
            }
        }
        ++drawn;
    }
    if (click_hit >= 0) {
        overlay_click_hit = true;
        g_sel_spawn_marker = click_hit;
        g_marker_clear_selection = true;
    }

    if (S.show_spawn_markers) {
        std::vector<LevelEdit::GeneratorAddition> pending;
        LevelEdit::GetGenerators(pending);
        int gen_click = -1;
        float gen_best = 12.0f * 12.0f;
        for (size_t gi = 0; gi < pending.size(); ++gi) {
            const auto& pg = pending[gi];
            if (pg.removed) continue;
            XMVECTOR clip = XMVector4Transform(
                XMVectorSet(pg.pos[0], pg.pos[2], pg.pos[1], 1.0f),
                VP);
            const float w = XMVectorGetW(clip);
            if (w <= 0.05f) continue;
            const float ndcx = XMVectorGetX(clip) / w;
            const float ndcy = XMVectorGetY(clip) / w;
            if (ndcx < -1.1f || ndcx > 1.1f) continue;
            if (ndcy < -1.1f || ndcy > 1.1f) continue;
            ImVec2 pt;
            pt.x = origin.x + (ndcx * 0.5f + 0.5f) * region.x;
            pt.y = origin.y +
                   (1.0f - (ndcy * 0.5f + 0.5f)) * region.y;
            const float r = 6.0f;
            dl->AddQuadFilled(ImVec2(pt.x, pt.y - r),
                              ImVec2(pt.x + r, pt.y),
                              ImVec2(pt.x, pt.y + r),
                              ImVec2(pt.x - r, pt.y),
                              IM_COL32(200, 120, 255, 235));
            const bool gsel = (int(gi) == g_sel_pending_gen);
            dl->AddQuad(ImVec2(pt.x, pt.y - r - 1),
                        ImVec2(pt.x + r + 1, pt.y),
                        ImVec2(pt.x, pt.y + r + 1),
                        ImVec2(pt.x - r - 1, pt.y),
                        gsel ? IM_COL32(255, 255, 255, 255)
                             : IM_COL32(0, 0, 0, 200),
                        gsel ? 2.0f : 1.0f);
            const std::string lbl = "new: " + pg.creature_name;
            dl->AddText(ImVec2(pt.x + r + 3.0f, pt.y - 7.0f),
                        IM_COL32(225, 190, 255, 235), lbl.c_str());
            if (can_pick && clicked) {
                const float dx = mouse.x - pt.x;
                const float dy = mouse.y - pt.y;
                const float d2 = dx * dx + dy * dy;
                if (d2 < gen_best) {
                    gen_best = d2;
                    gen_click = int(gi);
                }
            }
        }
        if (gen_click >= 0) {
            overlay_click_hit = true;
            g_sel_pending_gen = gen_click;
            g_sel_pending_sp = -1;
            g_sel_spawn_marker = -1;
            g_marker_clear_selection = true;
        }

        std::vector<LevelEdit::PendingSpawnPoint> psps;
        LevelEdit::GetPendingSpawnPoints(psps);
        int sp_click = -1;
        float sp_best = 12.0f * 12.0f;
        for (const auto& sp : psps) {
            XMVECTOR clip = XMVector4Transform(
                XMVectorSet(sp.pos[0], sp.pos[2], sp.pos[1], 1.0f),
                VP);
            const float w = XMVectorGetW(clip);
            if (w <= 0.05f) continue;
            const float ndcx = XMVectorGetX(clip) / w;
            const float ndcy = XMVectorGetY(clip) / w;
            if (ndcx < -1.1f || ndcx > 1.1f) continue;
            if (ndcy < -1.1f || ndcy > 1.1f) continue;
            ImVec2 pt;
            pt.x = origin.x + (ndcx * 0.5f + 0.5f) * region.x;
            pt.y = origin.y +
                   (1.0f - (ndcy * 0.5f + 0.5f)) * region.y;
            const float r = 4.5f;
            dl->AddQuadFilled(ImVec2(pt.x, pt.y - r),
                              ImVec2(pt.x + r, pt.y),
                              ImVec2(pt.x, pt.y + r),
                              ImVec2(pt.x - r, pt.y),
                              IM_COL32(225, 160, 255, 235));
            const bool spsel = (sp.id == g_sel_pending_sp);
            dl->AddQuad(ImVec2(pt.x, pt.y - r - 1),
                        ImVec2(pt.x + r + 1, pt.y),
                        ImVec2(pt.x, pt.y + r + 1),
                        ImVec2(pt.x - r - 1, pt.y),
                        spsel ? IM_COL32(255, 255, 255, 255)
                              : IM_COL32(0, 0, 0, 200),
                        spsel ? 2.0f : 1.0f);
            if (w < 30.0f || spsel) {
                dl->AddText(ImVec2(pt.x + r + 3.0f, pt.y - 7.0f),
                            IM_COL32(235, 205, 255, 235),
                            sp.label.c_str());
            }
            if (can_pick && clicked) {
                const float dx = mouse.x - pt.x;
                const float dy = mouse.y - pt.y;
                const float d2 = dx * dx + dy * dy;
                if (d2 < sp_best) {
                    sp_best = d2;
                    sp_click = sp.id;
                }
            }
        }
        if (sp_click >= 0) {
            overlay_click_hit = true;
            g_sel_pending_sp = sp_click;
            g_sel_pending_gen = -1;
            g_sel_spawn_marker = -1;
            g_marker_clear_selection = true;
        } else if (click_hit >= 0) {
            g_sel_pending_sp = -1;
            g_sel_pending_gen = -1;
        }
    }
    if (can_pick && (clicked || context_clicked) && !overlay_click_hit) {
        g_sel_spawn_marker = -1;
        g_sel_pending_sp = -1;
        g_sel_pending_gen = -1;
    }
    if (drawn) {
        char buf[96];
        std::snprintf(buf, sizeof(buf),
                      "entity markers: %zu shown / %zu total", drawn,
                      g_level_spawn_markers.size());
        dl->AddText(ImVec2(origin.x + 14, origin.y + region.y - 38),
                    IM_COL32(220, 220, 220, 200), buf);
    }
}

#endif
