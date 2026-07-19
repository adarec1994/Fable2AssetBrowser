std::vector<TransitionPoint> ExtractTransitionPoints(
    const std::vector<const std::vector<uint8_t>*>& gdbs,
    const std::vector<std::pair<uint32_t, std::string>>& hash_to_name)
{
    constexpr uint32_t level_exit_component = 0x30D88F7Eu;
    constexpr uint32_t world_connecting_to = 0x81F8308Eu;
    constexpr uint32_t entity_connecting_to = 0x86D461FDu;
    constexpr uint32_t teleporter = 0x8D8E8995u;
    constexpr uint32_t activated_on_interaction = 0x9A121353u;
    constexpr uint32_t level_connecting_to = 0xD1AD041Cu;
    constexpr uint32_t radius = 0xD422BC6Fu;

    std::vector<std::unique_ptr<GdbView>> owned;
    std::vector<const GdbView*> views;
    for (const auto* bytes : gdbs) {
        if (!bytes || bytes->empty()) continue;
        auto view = std::make_unique<GdbView>(*bytes);
        if (!view->ok) continue;
        views.push_back(view.get());
        owned.push_back(std::move(view));
    }
    if (views.empty()) return {};

    std::unordered_map<uint32_t, std::string> dict;
    for (const auto* bytes : gdbs) {
        if (!bytes || bytes->empty()) continue;
        auto entries = LoadEmbeddedDict(*bytes);
        dict.insert(entries.begin(), entries.end());
    }
    auto resolve_string = [&](uint32_t hash) -> std::string {
        const auto found = dict.find(hash);
        if (found != dict.end()) return found->second;
        return GdbHashName(hash, {});
    };
    auto read_string = [&](const MultiGdbCursor& record,
                           uint32_t field) -> std::string {
        MultiGdbCursor owner;
        uint32_t value = 0;
        uint8_t type = 0;
        if (!MultiFindInherited(views, record, field, 0xFF,
                                owner, value, &type) ||
            (type != 4 && type != 7) || value == 0 || value == kHashNull) {
            return {};
        }
        return resolve_string(value);
    };

    std::vector<TransitionPoint> out;
    out.reserve(hash_to_name.size() / 16 + 1);
    std::unordered_set<uint32_t> emitted;
    for (const auto& [entity_hash, entity_name] : hash_to_name) {
        if (entity_hash == 0 || !emitted.insert(entity_hash).second) continue;
        MultiGdbCursor entity;
        if (!MultiLookup(views, entity_hash, entity)) continue;

        MultiGdbCursor component_owner;
        uint32_t component_hash = 0;
        if (!MultiFindInherited(views, entity, level_exit_component, 6,
                                component_owner, component_hash)) {
            continue;
        }
        MultiGdbCursor component;
        if (!MultiLookup(views, component_hash, component)) continue;

        TransitionPoint point;
        point.entity_hash = entity_hash;
        point.component_record = component_hash;
        point.name = entity_name;
        point.world = read_string(component, world_connecting_to);
        point.level = read_string(component, level_connecting_to);
        point.destination_entity = read_string(component,
                                               entity_connecting_to);
        MultiReadInheritedFloat(views, component, radius, point.radius);
        int flag = 0;
        if (MultiReadInheritedBool(views, component,
                                   activated_on_interaction, flag)) {
            point.activated_on_interaction = flag != 0;
        }
        if (MultiReadInheritedBool(views, component, teleporter, flag)) {
            point.teleporter = flag != 0;
        }

        float rx = 0.0f, ry = 0.0f, rz = 0.0f;
        bool has_rotation = false;
        size_t pos_slots[3] = {0, 0, 0};
        size_t rot_slots[3] = {0, 0, 0};
        bool has_position = TryComponentTransformRecord(
            *entity.view, entity.record, point.x, point.y, point.z,
            rx, ry, rz, has_rotation, pos_slots, rot_slots);
        if (!has_position) {
            has_position = TryTransformRecord(
                *entity.view, entity.record, point.x, point.y, point.z,
                rx, ry, rz, has_rotation, pos_slots, rot_slots);
        }
        if (!has_position) {
            has_position = TryIndexedInlineTransform(
                *entity.view, entity.record, point.x, point.y, point.z,
                rx, ry, rz, has_rotation, pos_slots);
        }
        if (has_position) out.push_back(std::move(point));
    }
    return out;
}
