namespace {

constexpr uint32_t kHashAnimationManagerComponent = 0x7C63CDDFu;
constexpr uint32_t kHashAnimationsField           = 0xF96D7984u;
constexpr uint32_t kHashAnimationListField        = 0x63565E85u;
constexpr uint32_t kHashAnimationSetField         = 0xE227399Fu;
constexpr uint32_t kHashAnimGroupAliases          = 0x4CD76EB9u;

template <typename Fn>
void ForEachLocalField(const GdbView& view, size_t record, Fn&& fn)
{
    size_t schema_off = 0;
    uint32_t field_count = 0;
    if (!view.schema(record, schema_off, field_count)) return;
    const size_t hashes = schema_off + 4;
    const size_t descs = hashes + size_t(field_count) * 4;
    for (uint32_t i = 0; i < field_count; ++i) {
        const size_t slot = record + 4 + size_t(i) * 4;
        if (slot + 4 > view.body_end) return;
        const uint32_t field_hash =
            ReadBeU32(view.bytes.data() + hashes + size_t(i) * 4);
        const uint8_t type = uint8_t(
            ReadBeU32(view.bytes.data() + descs + size_t(i) * 4) >> 24);
        const uint32_t raw = ReadBeU32(view.bytes.data() + slot);
        fn(field_hash, type, raw);
    }
}

void CollectRefClipKeys(const std::vector<const GdbView*>& views,
                        uint32_t ref_hash,
                        std::vector<uint32_t>& out,
                        std::unordered_set<uint32_t>& visited,
                        int depth)
{
    if (depth > 4 || ref_hash == 0 || ref_hash == kHashNull) return;
    if (!visited.insert(ref_hash).second) return;
    MultiGdbCursor cur;
    if (!MultiLookup(views, ref_hash, cur)) return;
    for (int step = 0; step < 16; ++step) {
        uint32_t parent = 0;
        std::vector<uint32_t> nested;
        ForEachLocalField(*cur.view, cur.record,
                          [&](uint32_t fh, uint8_t type, uint32_t raw) {
            if (fh == kHashParent && type == 6) {
                parent = raw;
                return;
            }
            if (type == 4 || type == 7) {
                if (raw != 0 && raw != kHashNull) out.push_back(raw);
            } else if (type == 6) {
                nested.push_back(raw);
            }
        });
        for (uint32_t child : nested) {
            CollectRefClipKeys(views, child, out, visited, depth + 1);
        }
        if (parent == 0 || parent == kHashNull) return;
        if (!visited.insert(parent).second) return;
        MultiGdbCursor next;
        if (!MultiLookup(views, parent, next)) return;
        cur = next;
    }
}

void CollectAnimSlotsFromChain(
    const std::vector<const GdbView*>& views,
    uint32_t root_hash,
    const std::unordered_map<uint32_t, std::string>& dict,
    EntityAnimTree& tree,
    std::unordered_map<uint32_t, size_t>& slot_index)
{
    auto resolve_name = [&](uint32_t hash) -> std::string {
        const auto found = dict.find(hash);
        if (found != dict.end() && !found->second.empty()) {
            return found->second;
        }
        std::string named = GdbHashName(hash, {});
        if (!named.empty()) return named;
        return Hex32(hash);
    };

    uint32_t cur_hash = root_hash;
    std::unordered_set<uint32_t> chain_visited;
    for (int depth = 0; depth < 32; ++depth) {
        if (cur_hash == 0 || cur_hash == kHashNull ||
            !chain_visited.insert(cur_hash).second) {
            break;
        }
        MultiGdbCursor cur;
        if (!MultiLookup(views, cur_hash, cur)) break;
        tree.chain.push_back(cur_hash);

        uint32_t parent = 0;
        ForEachLocalField(*cur.view, cur.record,
                          [&](uint32_t fh, uint8_t type, uint32_t raw) {
            if (fh == kHashParent && type == 6) {
                parent = raw;
                return;
            }
            if (fh == kHashAnimGroupAliases ||
                fh == kHashAnimationsField ||
                fh == kHashAnimationListField ||
                fh == kHashAnimationSetField ||
                fh == kHashAnimationManagerComponent) {
                return;
            }
            if (type != 4 && type != 6 && type != 7) return;
            if (raw == 0 || raw == kHashNull) return;

            const auto existing = slot_index.find(fh);
            if (existing != slot_index.end()) {
                AnimTreeSlot& slot = tree.slots[existing->second];
                if (slot.chain_depth == depth &&
                    slot.owner_record == cur_hash) {
                    if (type == 6) {
                        std::unordered_set<uint32_t> visited;
                        CollectRefClipKeys(views, raw, slot.variation_keys,
                                           visited, 0);
                    } else {
                        slot.variation_keys.push_back(raw);
                    }
                } else {
                    slot.overridden_deeper = true;
                }
                return;
            }

            AnimTreeSlot slot;
            slot.slot_hash = fh;
            slot.slot_name = resolve_name(fh);
            slot.owner_record = cur_hash;
            slot.chain_depth = depth;
            if (type == 6) {
                slot.ref_record = raw;
                std::unordered_set<uint32_t> visited;
                CollectRefClipKeys(views, raw, slot.variation_keys,
                                   visited, 0);
                if (slot.variation_keys.empty()) return;
                slot.clip_key = slot.variation_keys.front();
                slot.variation_keys.erase(slot.variation_keys.begin());
            } else {
                slot.clip_key = raw;
            }
            slot_index.emplace(fh, tree.slots.size());
            tree.slots.push_back(std::move(slot));
        });

        cur_hash = parent;
    }
}

}

std::unordered_map<uint32_t, EntityAnimTree> ExtractEntityAnimTrees(
    const std::vector<const std::vector<uint8_t>*>& gdbs,
    const std::vector<std::pair<uint32_t, std::string>>& hash_to_name)
{
    std::vector<std::unique_ptr<GdbView>> owned;
    std::vector<const GdbView*> views;
    for (const auto* bytes : gdbs) {
        if (!bytes || bytes->empty()) continue;
        auto view = std::make_unique<GdbView>(*bytes);
        if (!view->ok) continue;
        views.push_back(view.get());
        owned.push_back(std::move(view));
    }
    std::unordered_map<uint32_t, EntityAnimTree> out;
    if (views.empty()) return out;

    std::unordered_map<uint32_t, std::string> dict;
    for (const auto* bytes : gdbs) {
        if (!bytes || bytes->empty()) continue;
        auto entries = LoadEmbeddedDict(*bytes);
        dict.insert(entries.begin(), entries.end());
    }

    std::unordered_map<uint32_t, EntityAnimTree> by_root;
    auto build_for_root = [&](uint32_t manager_hash,
                              uint32_t root_hash) -> const EntityAnimTree* {
        if (root_hash == 0 || root_hash == kHashNull) return nullptr;
        auto cached = by_root.find(root_hash);
        if (cached != by_root.end()) return &cached->second;
        EntityAnimTree tree;
        tree.manager_record = manager_hash;
        tree.animations_root = root_hash;
        std::unordered_map<uint32_t, size_t> slot_index;
        CollectAnimSlotsFromChain(views, root_hash, dict, tree, slot_index);
        if (manager_hash != 0 && manager_hash != root_hash) {
            CollectAnimSlotsFromChain(views, manager_hash, dict, tree,
                                      slot_index);
        }
        if (tree.slots.empty()) return nullptr;
        return &by_root.emplace(root_hash, std::move(tree)).first->second;
    };

    auto resolve_entity = [&](uint32_t entity_hash) {
        if (entity_hash == 0 || entity_hash == kHashNull) return;
        if (out.count(entity_hash)) return;
        MultiGdbCursor entity;
        if (!MultiLookup(views, entity_hash, entity)) return;

        MultiGdbCursor manager_field_owner;
        uint32_t manager_hash = 0;
        if (!MultiFindInherited(views, entity,
                                kHashAnimationManagerComponent, 6,
                                manager_field_owner, manager_hash)) {
            return;
        }
        MultiGdbCursor manager;
        if (!MultiLookup(views, manager_hash, manager)) return;

        uint32_t root_hash = 0;
        const uint32_t group_fields[] = {
            kHashAnimationsField, kHashAnimationListField,
            kHashAnimationSetField,
        };
        for (uint32_t field : group_fields) {
            MultiGdbCursor owner;
            uint32_t value = 0;
            if (MultiFindInherited(views, manager, field, 6, owner, value)) {
                root_hash = value;
                break;
            }
        }
        if (root_hash == 0) root_hash = manager_hash;

        const EntityAnimTree* tree = build_for_root(manager_hash, root_hash);
        if (!tree) return;
        EntityAnimTree copy = *tree;
        copy.entity_hash = entity_hash;
        out.emplace(entity_hash, std::move(copy));
    };

    for (const auto& [entity_hash, entity_name] : hash_to_name) {
        (void)entity_name;
        resolve_entity(entity_hash);
    }

    for (const GdbView* view : views) {
        const size_t count = std::min<size_t>(
            view->count, view->record_data_offsets.size());
        for (size_t i = 0; i < count; ++i) {
            const uint32_t record_hash =
                ReadBeU32(view->bytes.data() + view->hash_base + i * 4);
            if (record_hash == 0 || record_hash == kHashNull) continue;
            size_t slot_off = 0;
            if (!view->findLocal(view->record_data_offsets[i],
                                 kHashAnimationManagerComponent, 6,
                                 slot_off, nullptr)) {
                continue;
            }
            resolve_entity(record_hash);
        }
    }
    return out;
}
