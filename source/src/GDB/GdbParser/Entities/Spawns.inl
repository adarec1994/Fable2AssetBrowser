std::unordered_map<uint32_t, SpawnEntityInfo> CollectSpawnEntities(
    const std::vector<const std::vector<uint8_t>*>& gdbs,
    const std::vector<std::pair<uint32_t, std::string>>& hash_to_name,
    SpawnDonorInfo* out_donor,
    NpcDonorInfo* out_npc_donor)
{
    std::unordered_map<uint32_t, SpawnEntityInfo> out;
    constexpr uint32_t kCreatureGenerator = 0xA2371C5Au;
    constexpr uint32_t kCreatureGeneratorSpawnPoint = 0x110071ADu;
    constexpr uint32_t kCreatureSpawnPoint = 0x1054A35Cu;
    constexpr uint32_t kSimpleTransformComponent = 0x619F96CFu;

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
    if (views.empty() || hash_to_name.empty()) return out;
    const MultiGdbRecordIndex record_index =
        BuildMultiGdbRecordIndex(views);

    constexpr uint32_t kCharNavComponent = 0xC5A11B7Au;
    const GdbView* level_view = views.front();
    auto resolve_pos = [&](const GdbView& view, size_t rec,
                           SpawnEntityInfo& info) {
        float x = 0, y = 0, z = 0;
        float rx = 0, ry = 0, rz = 0;
        bool hr = false;
        size_t ps[3] = {0, 0, 0};
        size_t rs[3] = {0, 0, 0};
        if (TryComponentTransformRecord(view, rec, x, y, z, rx, ry, rz,
                                        hr, ps, rs) ||
            TryComponentTransformField(view, rec,
                                       kSimpleTransformComponent, x, y,
                                       z, rx, ry, rz, hr, ps, rs) ||
            TryComponentTransformField(view, rec, kCharNavComponent, x,
                                       y, z, rx, ry, rz, hr, ps, rs) ||
            TryTransformRecord(view, rec, x, y, z, rx, ry, rz, hr, ps,
                               rs)) {
            info.has_pos = true;
            info.x = x;
            info.y = y;
            info.z = z;
            info.has_rotation = hr;
            info.rot_x = rx;
            info.rot_y = ry;
            info.rot_z = rz;
            if (&view == level_view) {
                for (int k = 0; k < 3; ++k) {
                    info.pos_off[k] = uint32_t(ps[k]);
                    info.rot_off[k] = uint32_t(rs[k]);
                }
            }
        }
    };

    auto fill_donor_entity = [&](const GdbView& view, size_t rec,
                                 uint32_t& out_template,
                                 uint32_t& out_comp_field,
                                 uint32_t& out_comp_parent,
                                 uint32_t& out_tf_field,
                                 uint32_t& out_tf_parent,
                                 uint32_t& out_pos_parent,
                                 uint32_t& out_rot_parent,
                                 uint32_t comp_field_hash) {
        size_t slot = 0;
        if (view.findLocal(rec, kHashParent, 6, slot, nullptr)) {
            out_template = ReadBeU32(view.bytes.data() + slot);
        }
        if (view.findLocal(rec, comp_field_hash, 6, slot, nullptr)) {
            out_comp_field = comp_field_hash;
            const uint32_t ch = ReadBeU32(view.bytes.data() + slot);
            size_t crec = 0;
            if (view.lookup(ch, crec)) {
                size_t ps2 = 0;
                if (view.findLocal(crec, kHashParent, 6, ps2, nullptr)) {
                    out_comp_parent =
                        ReadBeU32(view.bytes.data() + ps2);
                }
            }
        }
        const uint32_t tf_candidates[] = {
            kHashTransformComponent,
            kSimpleTransformComponent,
            kHashPhysicsSimulationKeyframedComponent,
            kHashPhysicsSimulationStaticComponent,
            0xFC8A57C5u,
            kCharNavComponent,
        };
        for (uint32_t tf : tf_candidates) {
            size_t s2 = 0;
            if (!view.findLocal(rec, tf, 6, s2, nullptr)) continue;
            const uint32_t ch = ReadBeU32(view.bytes.data() + s2);
            size_t crec = 0;
            if (!view.lookup(ch, crec)) continue;
            size_t pslot = 0, powner = 0;
            if (!view.findFieldOwner(crec, kHashPosition, 6, pslot,
                                     powner, nullptr)) {
                continue;
            }
            out_tf_field = tf;
            size_t ps2 = 0;
            if (view.findLocal(crec, kHashParent, 6, ps2, nullptr)) {
                out_tf_parent = ReadBeU32(view.bytes.data() + ps2);
            }
            auto capture_vector_parent = [&](uint32_t field_hash,
                                             uint32_t& out_parent) {
                size_t fslot = 0, fowner = 0;
                if (!view.findFieldOwner(crec, field_hash, 6, fslot,
                                         fowner, nullptr)) {
                    return;
                }
                const uint32_t vec_hash =
                    ReadBeU32(view.bytes.data() + fslot);
                size_t vec_rec = 0, vec_parent_slot = 0;
                if (view.lookup(vec_hash, vec_rec) &&
                    view.findLocal(vec_rec, kHashParent, 6,
                                   vec_parent_slot, nullptr)) {
                    out_parent = ReadBeU32(
                        view.bytes.data() + vec_parent_slot);
                }
            };
            capture_vector_parent(kHashPosition, out_pos_parent);
            capture_vector_parent(kHashRotation, out_rot_parent);
            break;
        }
    };

    constexpr uint32_t kSpawnedCreatureName = 0x2A80DD7Bu;
    constexpr uint32_t kSpawnedCreatureDefault = 0x9FFE461Eu;
    constexpr uint32_t kCreatureComponent = 0xC3B90D4Fu;

    std::unordered_map<uint32_t, uint32_t> name_fnv_to_entity;
    name_fnv_to_entity.reserve(hash_to_name.size() * 2);
    for (const auto& kv : hash_to_name) {
        uint32_t h = 0x811C9DC5u;
        for (unsigned char c : kv.second) {
            h *= 0x01000193u;
            h ^= c;
        }
        name_fnv_to_entity.emplace(h, kv.first);
    }
    for (const GdbView* vw : views) {
        if (vw->bytes.size() < 0x18) continue;
        const uint32_t pairs = ReadBeU32(vw->bytes.data() + 0x10);
        const size_t meta_end =
            vw->offset_base + size_t(vw->count) * 2;
        const size_t name_base = (meta_end + 3) & ~size_t(3);
        if (name_base + size_t(pairs) * 8 > vw->bytes.size()) continue;
        for (uint32_t i = 0; i < pairs; ++i) {
            const uint32_t a =
                ReadBeU32(vw->bytes.data() + name_base + size_t(i) * 8);
            const uint32_t b = ReadBeU32(vw->bytes.data() + name_base +
                                         size_t(i) * 8 + 4);
            name_fnv_to_entity.emplace(a, b);
            name_fnv_to_entity.emplace(b, a);
        }
    }

    auto models_for_entity = [&](uint32_t entity_hash) {
        return ModelsForEntityMulti(record_index, entity_hash);
    };

    std::unordered_map<uint32_t, std::string> fnv_to_name;
    fnv_to_name.reserve(hash_to_name.size() * 2);
    for (const auto& kv : hash_to_name) {
        uint32_t h = 0x811C9DC5u;
        for (unsigned char c : kv.second) {
            h *= 0x01000193u;
            h ^= c;
        }
        fnv_to_name.emplace(h, kv.second);
    }

    for (const auto& kv : hash_to_name) {
        const uint32_t entity_hash = kv.first;
        MultiGdbCursor ent;
        if (!MultiLookup(views, entity_hash, ent)) continue;
        SpawnEntityInfo info;
        MultiGdbCursor owner;
        uint32_t comp = 0;
        if (MultiFindInherited(views, ent, kCreatureGenerator, 6, owner,
                               comp) &&
            comp != 0 && comp != kHashNull) {
            info.kind = 1;
            MultiGdbCursor gen;
            if (MultiLookup(views, comp, gen)) {
                constexpr uint32_t kSpawnPointsField = 0x559B5DBFu;
                {
                    MultiGdbCursor spo;
                    uint32_t sp_list = 0;
                    if (MultiFindInherited(views, gen,
                                           kSpawnPointsField, 6, spo,
                                           sp_list) &&
                        sp_list != 0 && sp_list != kHashNull) {
                        info.spawn_points_record = sp_list;
                        MultiGdbCursor sl;
                        if (MultiLookup(views, sp_list, sl)) {
                            for (int depth = 0; depth < 8; ++depth) {
                                size_t sch = 0;
                                uint32_t nf = 0;
                                if (!sl.view->schema(sl.record, sch,
                                                     nf)) {
                                    break;
                                }
                                const size_t fh0 = sch + 4;
                                for (uint32_t i2 = 0; i2 < nf; ++i2) {
                                    const uint32_t fh = ReadBeU32(
                                        sl.view->bytes.data() + fh0 +
                                        size_t(i2) * 4);
                                    if (fh == kHashParent) continue;
                                    const uint32_t vh = ReadBeU32(
                                        sl.view->bytes.data() +
                                        sl.record + 4 +
                                        size_t(i2) * 4);
                                    if (vh && vh != kHashNull) {
                                        info.spawn_point_entities
                                            .push_back(vh);
                                    }
                                }
                                size_t pslot = 0;
                                if (!sl.view->findLocal(sl.record,
                                                        kHashParent, 6,
                                                        pslot,
                                                        nullptr)) {
                                    break;
                                }
                                const uint32_t ph = ReadBeU32(
                                    sl.view->bytes.data() + pslot);
                                MultiGdbCursor nxt;
                                if (!MultiLookup(views, ph, nxt)) break;
                                sl = nxt;
                            }
                        }
                    }
                }
                if (out_donor && !out_donor->gen_template &&
                    ent.view == level_view) {
                    fill_donor_entity(*ent.view, ent.record,
                                      out_donor->gen_template,
                                      out_donor->gen_comp_field,
                                      out_donor->gen_comp_parent,
                                      out_donor->gen_transform_field,
                                      out_donor->gen_transform_parent,
                                      out_donor->gen_position_parent,
                                      out_donor->gen_rotation_parent,
                                      kCreatureGenerator);
                    if (!out_donor->gen_template ||
                        !out_donor->gen_comp_field ||
                        !out_donor->gen_transform_field) {
                        out_donor->gen_template = 0;
                        out_donor->gen_comp_field = 0;
                        out_donor->gen_comp_parent = 0;
                        out_donor->gen_transform_field = 0;
                        out_donor->gen_transform_parent = 0;
                        out_donor->gen_position_parent = 0;
                        out_donor->gen_rotation_parent = 0;
                    }
                    if (info.spawn_points_record) {
                        size_t lrec = 0;
                        if (level_view->lookup(info.spawn_points_record,
                                               lrec)) {
                            size_t ps2 = 0;
                            if (level_view->findLocal(lrec, kHashParent,
                                                      6, ps2,
                                                      nullptr)) {
                                out_donor->spawn_list_parent = ReadBeU32(
                                    level_view->bytes.data() + ps2);
                            }
                        }
                    }
                }
                MultiGdbCursor no;
                uint32_t name_fnv = 0;
                uint8_t ty = 0;
                if (MultiFindInherited(views, gen, kSpawnedCreatureName,
                                       0xFF, no, name_fnv, &ty) &&
                    (ty == 4 || ty == 7) &&
                    name_fnv != 0 && name_fnv != kHashNull &&
                    name_fnv != kSpawnedCreatureDefault) {
                    auto fit = fnv_to_name.find(name_fnv);
                    if (fit != fnv_to_name.end()) {
                        info.creature_name = fit->second;
                    }
                    auto nit = name_fnv_to_entity.find(name_fnv);
                    if (nit != name_fnv_to_entity.end()) {
                        info.creature_entity_hash = nit->second;
                        info.creature_entity_candidates.push_back(
                            nit->second);
                        info.model_hashes =
                            models_for_entity(nit->second);
                    }
                }
                if (info.model_hashes.empty()) {
                    constexpr uint32_t kFamilies = 0xF44CE155u;
                    constexpr uint32_t kCreatures = 0xA1F7A17Du;
                    auto visit_list_records =
                        [&](MultiGdbCursor list, auto&& visitor) {
                            std::unordered_set<uint32_t> seen_parents;
                            for (int depth = 0; depth < 16; ++depth) {
                                size_t schema = 0;
                                uint32_t field_count = 0;
                                if (!list.view->schema(list.record, schema,
                                                       field_count)) {
                                    break;
                                }
                                const size_t field_hashes = schema + 4;
                                const size_t descriptors =
                                    field_hashes + size_t(field_count) * 4;
                                uint32_t parent_hash = 0;
                                for (uint32_t field_index = 0;
                                     field_index < field_count;
                                     ++field_index) {
                                    const uint32_t field_hash = ReadBeU32(
                                        list.view->bytes.data() +
                                        field_hashes +
                                        size_t(field_index) * 4);
                                    const uint8_t field_type = uint8_t(
                                        ReadBeU32(list.view->bytes.data() +
                                                  descriptors +
                                                  size_t(field_index) * 4) >>
                                        24);
                                    const uint32_t value = ReadBeU32(
                                        list.view->bytes.data() + list.record +
                                        4 + size_t(field_index) * 4);
                                    if (field_hash == kHashParent &&
                                        field_type == 6) {
                                        parent_hash = value;
                                        continue;
                                    }
                                    if (!visitor(field_hash, field_type,
                                                 value)) {
                                        return false;
                                    }
                                }
                                if (parent_hash == 0 ||
                                    parent_hash == kHashNull ||
                                    !seen_parents.insert(parent_hash).second) {
                                    break;
                                }
                                MultiGdbCursor parent;
                                if (!MultiLookup(views, parent_hash, parent)) {
                                    break;
                                }
                                list = parent;
                            }
                            return true;
                        };
                    MultiGdbCursor fo;
                    uint32_t fam_list = 0;
                    if (MultiFindInherited(views, gen, kFamilies, 6, fo,
                                           fam_list) &&
                        fam_list != 0 && fam_list != kHashNull) {
                        MultiGdbCursor fl;
                        if (MultiLookup(views, fam_list, fl)) {
                            visit_list_records(
                                fl,
                                [&](uint32_t, uint8_t family_type,
                                    uint32_t fam_hash) {
                                    if (family_type != 6 || fam_hash == 0 ||
                                        fam_hash == kHashNull) {
                                        return true;
                                    }
                                    MultiGdbCursor fam;
                                    if (!MultiLookup(views, fam_hash,
                                                     fam)) {
                                        return true;
                                    }
                                    MultiGdbCursor co2;
                                    uint32_t creatures = 0;
                                    if (!MultiFindInherited(
                                            views, fam, kCreatures, 6,
                                            co2, creatures) ||
                                        creatures == 0 ||
                                        creatures == kHashNull) {
                                        return true;
                                    }
                                    MultiGdbCursor cl;
                                    if (!MultiLookup(views, creatures,
                                                     cl)) {
                                        return true;
                                    }
                                    visit_list_records(
                                        cl,
                                        [&](uint32_t creature_field_hash,
                                            uint8_t creature_type,
                                            uint32_t creature_hash) {
                                            if ((creature_type != 7 &&
                                                 creature_type != 4) ||
                                                creature_hash == 0 ||
                                                creature_hash == kHashNull) {
                                                return true;
                                            }








                                            for (uint32_t candidate :
                                                 {creature_hash,
                                                  creature_field_hash}) {
                                                if (candidate == 0 ||
                                                    candidate == kHashNull ||
                                                    candidate == kHashParent) {
                                                    continue;
                                                }
                                                if (std::find(
                                                        info.creature_entity_candidates
                                                            .begin(),
                                                        info.creature_entity_candidates
                                                            .end(),
                                                        candidate) ==
                                                    info.creature_entity_candidates
                                                        .end()) {
                                                    info.creature_entity_candidates
                                                        .push_back(candidate);
                                                }
                                                std::vector<uint32_t> models =
                                                    models_for_entity(candidate);
                                                if (models.empty()) continue;
                                                info.model_hashes =
                                                    std::move(models);
                                                info.creature_entity_hash =
                                                    candidate;
                                                return false;
                                            }
                                            return true;
                                        });
                                    return info.model_hashes.empty();
                                });
                        }
                    }
                }
            }
        } else if ((MultiFindInherited(views, ent,
                                       kCreatureGeneratorSpawnPoint, 6,
                                       owner, comp) ||
                    MultiFindInherited(views, ent, kCreatureSpawnPoint,
                                       6, owner, comp)) &&
                   comp != 0 && comp != kHashNull) {
            info.kind = 2;
            if (out_donor && !out_donor->sp_template &&
                ent.view == level_view) {
                for (uint32_t cf : {kCreatureGeneratorSpawnPoint,
                                    kCreatureSpawnPoint}) {
                    size_t s3 = 0;
                    if (!ent.view->findLocal(ent.record, cf, 6, s3,
                                             nullptr)) {
                        continue;
                    }
                    fill_donor_entity(*ent.view, ent.record,
                                      out_donor->sp_template,
                                      out_donor->sp_comp_field,
                                      out_donor->sp_comp_parent,
                                      out_donor->sp_transform_field,
                                      out_donor->sp_transform_parent,
                                      out_donor->sp_position_parent,
                                      out_donor->sp_rotation_parent,
                                      cf);
                    if (!out_donor->sp_template ||
                        !out_donor->sp_comp_field ||
                        !out_donor->sp_transform_field) {
                        out_donor->sp_template = 0;
                        out_donor->sp_comp_field = 0;
                        out_donor->sp_comp_parent = 0;
                        out_donor->sp_transform_field = 0;
                        out_donor->sp_transform_parent = 0;
                        out_donor->sp_position_parent = 0;
                        out_donor->sp_rotation_parent = 0;
                        continue;
                    }
                    break;
                }
            }
        } else {
            MultiGdbCursor so;
            uint32_t skel = 0;
            uint8_t sty = 0;
            if ((MultiFindInherited(views, ent, kHashSkeletonFile, 0xFF,
                                    so, skel, &sty) &&
                 (sty == 4 || sty == 7) && skel != 0 &&
                 skel != kHashNull) ||
                (MultiFindInherited(views, ent, kCreatureComponent, 6,
                                    so, skel) &&
                 skel != 0 && skel != kHashNull)) {
                info.kind = 3;
                info.model_hashes = models_for_entity(entity_hash);
                if (out_npc_donor && !out_npc_donor->valid() &&
                    ent.view == level_view) {
                    uint32_t unused_template = 0;
                    uint32_t unused_component_field = 0;
                    uint32_t unused_component_parent = 0;
                    fill_donor_entity(
                        *ent.view, ent.record, unused_template,
                        unused_component_field, unused_component_parent,
                        out_npc_donor->transform_field,
                        out_npc_donor->transform_parent,
                        out_npc_donor->position_parent,
                        out_npc_donor->rotation_parent, 0);
                    if (!out_npc_donor->valid()) {
                        *out_npc_donor = NpcDonorInfo{};
                    }
                }
            }
        }
        if (!info.kind) continue;
        resolve_pos(*ent.view, ent.record, info);
        out.emplace(entity_hash, info);
    }
    return out;
}
