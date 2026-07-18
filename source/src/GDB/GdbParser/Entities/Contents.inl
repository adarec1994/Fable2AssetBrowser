std::unordered_map<uint32_t, EntityContents> ExtractEntityContents(
    const std::vector<const std::vector<uint8_t>*>& gdbs,
    const std::vector<std::pair<uint32_t, std::string>>& hash_to_name)
{
    std::unordered_map<uint32_t, EntityContents> out;

    std::vector<std::unique_ptr<GdbView>> owned;
    std::vector<const GdbView*> views;
    for (const auto* g : gdbs) {
        if (!g || g->empty()) continue;
        auto v = std::make_unique<GdbView>(*g);
        if (v->ok) {
            views.push_back(v.get());
            owned.push_back(std::move(v));
        }
    }
    if (views.empty()) return out;

    std::unordered_map<uint32_t, std::string> dict;
    for (const auto* g : gdbs) {
        if (!g || g->empty()) continue;
        auto d = LoadEmbeddedDict(*g);
        dict.insert(d.begin(), d.end());
    }
    auto dict_name = [&](uint32_t h) -> std::string {
        auto it = dict.find(h);
        if (it != dict.end() && !it->second.empty()) return it->second;
        return GdbHashName(h, {});
    };

    auto read_items_list = [&](uint32_t list_hash,
                               std::vector<EntityContentsItem>& items) {
        MultiGdbCursor list;
        if (!MultiLookup(views, list_hash, list)) return;
        size_t sch = 0;
        uint32_t n = 0;
        if (!list.view->schema(list.record, sch, n)) return;
        const size_t hashes = sch + 4;
        const size_t descs = hashes + size_t(n) * 4;
        for (uint32_t i = 0; i < n && items.size() < 64; ++i) {
            const uint32_t fh =
                ReadBeU32(list.view->bytes.data() + hashes + size_t(i) * 4);
            if (fh == kHashParent) continue;
            const uint32_t desc =
                ReadBeU32(list.view->bytes.data() + descs + size_t(i) * 4);
            const uint8_t type = uint8_t(desc >> 24);
            if (type != 4 && type != 6 && type != 7) continue;
            const size_t slot = list.record + 4 + size_t(i) * 4;
            if (slot + 4 > list.view->body_end) break;
            const uint32_t item_hash =
                ReadBeU32(list.view->bytes.data() + slot);
            if (item_hash == 0 || item_hash == kHashNull) continue;
            EntityContentsItem item;
            item.entry_label = dict_name(fh);
            ReadContentsItemInfo(views, item_hash, item, &dict);
            items.push_back(std::move(item));
        }
    };

    auto read_loot_table = [&](uint32_t table_hash,
                               std::vector<EntityContentsItem>& items) {
        MultiGdbCursor table;
        if (!MultiLookup(views, table_hash, table)) return;
        size_t sch = 0;
        uint32_t n = 0;
        if (!table.view->schema(table.record, sch, n)) return;
        const size_t hashes = sch + 4;
        const size_t descs = hashes + size_t(n) * 4;
        for (uint32_t i = 0; i < n && items.size() < 96; ++i) {
            const uint32_t fh =
                ReadBeU32(table.view->bytes.data() + hashes + size_t(i) * 4);
            if (fh == kHashParent) continue;
            const uint32_t desc =
                ReadBeU32(table.view->bytes.data() + descs + size_t(i) * 4);
            if (uint8_t(desc >> 24) != 6) continue;
            const size_t slot = table.record + 4 + size_t(i) * 4;
            if (slot + 4 > table.view->body_end) break;
            const uint32_t entry_hash =
                ReadBeU32(table.view->bytes.data() + slot);
            MultiGdbCursor entry;
            if (!MultiLookup(views, entry_hash, entry)) continue;

            MultiGdbCursor owner;
            uint32_t item_ref = 0;
            uint8_t ref_type = 0;
            if (!MultiFindInherited(views, entry, kHashItemReference, 0xFF,
                                    owner, item_ref, &ref_type) ||
                (ref_type != 4 && ref_type != 7) ||
                item_ref == 0 || item_ref == kHashNull) {
                continue;
            }

            EntityContentsItem item;
            item.entry_record_hash = entry_hash;
            item.entry_label = dict_name(fh);
            ReadContentsItemInfo(views, item_ref, item, &dict);
            MultiReadInheritedFloat(views, entry, kHashLootWeight,
                                    item.weight);
            MultiReadInheritedFloat(views, entry,
                                    kHashMinFurnitureRating,
                                    item.min_furniture_rating);
            MultiReadInheritedFloat(views, entry,
                                    kHashMaxFurnitureRating,
                                    item.max_furniture_rating);
            MultiReadInheritedFloat(views, entry, kHashMinChapterProgress,
                                    item.min_chapter);
            MultiReadInheritedFloat(views, entry, kHashMaxChapterProgress,
                                    item.max_chapter);
            items.push_back(std::move(item));
        }
    };

    for (const auto& [entity_hash, entity_name] : hash_to_name) {
        MultiGdbCursor rec;
        if (!MultiLookup(views, entity_hash, rec)) continue;

        EntityContents ec;
        ec.entity_name = entity_name;
        {
            size_t parent_slot = 0;
            if (rec.view->findLocal(rec.record, kHashParent, 6,
                                    parent_slot, nullptr)) {
                ec.entity_template =
                    ReadBeU32(rec.view->bytes.data() + parent_slot);
            }
            const auto model_hashes =
                CollectModelPathHashesForRecord(*rec.view, rec.record);
            if (!model_hashes.empty()) {
                ec.model_path_hash = model_hashes.front();
            }

            constexpr uint32_t kTransformFields[] = {
                kHashTransformComponent,
                kHashSimpleTransformComponent,
                kHashPhysicsSimulationKeyframedComponent,
                kHashPhysicsSimulationStaticComponent,
                kHashPhysicsSimulationDynamicComponent,
            };
            for (uint32_t field_hash : kTransformFields) {
                size_t component_slot = 0;
                if (!rec.view->findLocal(rec.record, field_hash, 6,
                                         component_slot, nullptr)) {
                    continue;
                }
                const uint32_t component_hash = ReadBeU32(
                    rec.view->bytes.data() + component_slot);
                size_t component_record = 0;
                if (!rec.view->lookup(component_hash, component_record)) {
                    continue;
                }
                ec.transform_component_field = field_hash;
                size_t component_parent_slot = 0;
                if (rec.view->findLocal(component_record, kHashParent, 6,
                                        component_parent_slot, nullptr)) {
                    ec.transform_component_template = ReadBeU32(
                        rec.view->bytes.data() + component_parent_slot);
                }
                constexpr uint32_t kPhysicsFile = 0x92F5FEEEu;
                MultiGdbCursor component{rec.view, component_record};
                MultiGdbCursor physics_owner;
                uint32_t physics_hash = 0;
                uint8_t physics_type = 0;
                if (MultiFindInherited(views, component, kPhysicsFile,
                                       0xFF, physics_owner, physics_hash,
                                       &physics_type) &&
                    (physics_type == 4 || physics_type == 7) &&
                    physics_hash != 0 && physics_hash != kHashNull) {
                    ec.physics_file_hash = physics_hash;
                }
                break;
            }
        }
        {
            float rx = 0.0f, ry = 0.0f, rz = 0.0f;
            bool has_rotation = false;
            size_t pos_slots[3] = {0, 0, 0};
            size_t rot_slots[3] = {0, 0, 0};
            ec.has_pos = TryComponentTransformRecord(
                *rec.view, rec.record, ec.x, ec.y, ec.z,
                rx, ry, rz, has_rotation, pos_slots, rot_slots);
            if (!ec.has_pos) {
                ec.has_pos = TryTransformRecord(
                    *rec.view, rec.record, ec.x, ec.y, ec.z,
                    rx, ry, rz, has_rotation, pos_slots, rot_slots);
            }
            if (!ec.has_pos) {
                ec.has_pos = TryIndexedInlineTransform(
                    *rec.view, rec.record, ec.x, ec.y, ec.z,
                    rx, ry, rz, has_rotation, pos_slots);
            }
            if (ec.has_pos) {
                for (int i = 0; i < 3; ++i) {
                    ec.pos_off[i] = uint32_t(pos_slots[i]);
                    ec.rot_off[i] = uint32_t(rot_slots[i]);
                }
            }
        }

        MultiGdbCursor owner;
        uint32_t comp_hash = 0;
        if (MultiFindInherited(views, rec, kHashChestComponent, 6,
                               owner, comp_hash)) {
            ec.has_chest_component = true;
            MultiGdbCursor comp;
            if (MultiLookup(views, comp_hash, comp)) {
                MultiGdbCursor keys_owner;
                uint32_t keys_raw = 0;
                uint8_t keys_type = 0;
                if (MultiFindInherited(views, comp, kHashSilverKeysNeeded,
                                       0xFF, keys_owner, keys_raw,
                                       &keys_type) &&
                    (keys_type == 1 || keys_type == 5)) {
                    ec.silver_keys_needed = int(keys_raw);
                }
            }
        }

        uint32_t dig_comp_hash = 0;
        if (MultiFindInherited(views, rec, kHashDiggingSpotComponent, 6,
                               owner, dig_comp_hash) &&
            dig_comp_hash != 0 && dig_comp_hash != kHashNull) {
            ec.is_dig_spot = true;
        }

        uint32_t dive_comp_hash = 0;
        if (MultiFindInherited(views, rec, kHashDiveSpotComponent, 6,
                               owner, dive_comp_hash) &&
            dive_comp_hash != 0 && dive_comp_hash != kHashNull) {
            ec.is_dive_spot = true;
        }

        comp_hash = 0;
        if (MultiFindInherited(views, rec, kHashDogLeadToComponent, 6,
                               owner, comp_hash) &&
            comp_hash != 0 && comp_hash != kHashNull) {
            ec.dog_can_lead_to = true;
            MultiGdbCursor comp;
            if (MultiLookup(views, comp_hash, comp)) {
                MultiGdbCursor value_owner;
                uint32_t priority_raw = 0;
                uint8_t priority_type = 0;
                if (MultiFindInherited(views, comp, kHashDogLeadPriority,
                                       0xFF, value_owner, priority_raw,
                                       &priority_type) &&
                    (priority_type == 1 || priority_type == 5)) {
                    ec.dog_lead_priority = int(priority_raw);
                }
                MultiReadInheritedFloat(views, comp, kHashDogLeadRadius,
                                        ec.dog_lead_radius);
            }
        }

        uint32_t inv_hash = 0;
        if (!MultiFindInherited(views, rec, kHashInventoryComponentA, 6,
                                owner, inv_hash)) {
            MultiFindInherited(views, rec, kHashInventoryComponentB, 6,
                               owner, inv_hash);
        }
        if (inv_hash != 0 && inv_hash != kHashNull) {
            ec.has_inventory_component = true;
            ec.inventory_component_record = inv_hash;
            MultiGdbCursor inv;
            if (MultiLookup(views, inv_hash, inv)) {
                MultiGdbCursor items_owner;
                uint32_t items_hash = 0;
                uint8_t items_type = 0;
                if (MultiFindInherited(views, inv, kHashInitialItems, 0xFF,
                                       items_owner, items_hash,
                                       &items_type) &&
                    (items_type == 6 || items_type == 7) &&
                    items_hash != 0 && items_hash != kHashNull) {
                    ec.initial_items_record = items_hash;
                    read_items_list(items_hash, ec.initial_items);
                }

                MultiGdbCursor repop_owner;
                uint32_t repop_hash = 0;
                if (MultiFindInherited(views, inv, kHashItemRepopulationData,
                                       6, repop_owner, repop_hash) &&
                    repop_hash != 0 && repop_hash != kHashNull) {
                    ec.item_repopulation_record = repop_hash;
                    MultiGdbCursor repop;
                    if (MultiLookup(views, repop_hash, repop)) {
                        MultiReadInheritedBool(views, repop,
                                               kHashCanBeStolenFrom,
                                               ec.can_be_stolen_from);
                        MultiReadInheritedBool(views, repop,
                                               kHashCanRespawnItemsOnce,
                                               ec.can_respawn_items_once);
                        MultiReadInheritedBool(
                            views, repop, kHashCanRespawnRepeatedly,
                            ec.can_respawn_items_repeatedly);
                        MultiReadInheritedFloat(views, repop,
                                                kHashChanceOfRespawning,
                                                ec.chance_of_respawning);
                        MultiGdbCursor pot_owner;
                        uint32_t pot_hash = 0;
                        uint8_t pot_type = 0;
                        if (MultiFindInherited(views, repop,
                                               kHashPotentialItems, 0xFF,
                                               pot_owner, pot_hash,
                                               &pot_type) &&
                            (pot_type == 6 || pot_type == 7) &&
                            pot_hash != 0 && pot_hash != kHashNull) {
                            ec.potential_items_record = pot_hash;
                            read_loot_table(pot_hash, ec.potential_items);
                        }
                    }
                }
            }
        }

        if (ec.has_inventory_component || ec.has_chest_component ||
            ec.is_dig_spot ||
            !ec.initial_items.empty() ||
            !ec.potential_items.empty()) {
            out.emplace(entity_hash, std::move(ec));
        }
    }

    return out;
}
