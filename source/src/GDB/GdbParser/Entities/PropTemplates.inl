std::unordered_map<uint32_t, PropTemplateInfo> BuildPropTemplateIndex(
    const std::vector<const std::vector<uint8_t>*>& gdbs)
{
    std::unordered_map<uint32_t, PropTemplateInfo> out;

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

    constexpr uint32_t kGraphicsComponents[] = {
        kHashGraphicAppearanceComponent,
        kHashGraphicAppearanceAnimatedMeshComponent,
        kHashStaticMeshComponent,
        kHashStaticMultipleMeshComponent,
        0x31FF8FCFu,
        0x515A75DAu,
    };

    constexpr uint32_t kPhysicsComponents[] = {
        kHashTransformComponent,
        kHashPhysicsSimulationKeyframedComponent,
        kHashPhysicsSimulationStaticComponent,
        0xFC8A57C5u,
    };
    constexpr uint32_t kHashPhysicsFile = 0x92F5FEEEu;
    constexpr uint32_t kInventoryComponents[] = {
        0x1C7D7B74u,
        0x73AB8B6Au,
    };

    for (const GdbView* vw : views) {
        const GdbView& view = *vw;
        for (uint32_t i = 0; i < view.count; ++i) {
            if (i >= view.record_data_offsets.size()) break;
            const size_t rec = view.record_data_offsets[i];




            bool is_placed_entity = false;
            for (uint32_t ic : kInventoryComponents) {
                size_t slot = 0;
                if (view.findLocal(rec, ic, 6, slot, nullptr)) {
                    is_placed_entity = true;
                    break;
                }
            }
            if (is_placed_entity) continue;

            bool has_graphics = false;
            for (uint32_t gc : kGraphicsComponents) {
                size_t slot = 0;
                if (view.findLocal(rec, gc, 6, slot, nullptr)) {
                    has_graphics = true;
                    break;
                }
            }
            if (!has_graphics) {
                bool has_local_physics = false;
                for (uint32_t pc : kPhysicsComponents) {
                    size_t slot = 0;
                    if (view.findLocal(rec, pc, 6, slot, nullptr)) {
                        has_local_physics = true;
                        break;
                    }
                }
                if (!has_local_physics) continue;
                float ix = 0, iy = 0, iz = 0;
                float rx = 0, ry = 0, rz = 0;
                bool has_rot = false;
                if (TryComponentTransformRecord(view, rec, ix, iy, iz,
                                                rx, ry, rz, has_rot)) {
                    continue;
                }
                for (uint32_t gc : kGraphicsComponents) {
                    size_t slot = 0;
                    if (view.findField(rec, gc, 6, slot, nullptr)) {
                        has_graphics = true;
                        break;
                    }
                }
            }
            if (!has_graphics) continue;

            const std::vector<uint32_t> model_hashes =
                CollectModelPathHashesForRecord(view, rec);
            if (model_hashes.empty()) continue;

            PropTemplateInfo info;
            info.template_hash =
                ReadBeU32(view.bytes.data() + view.hash_base +
                          size_t(i) * 4);

            MultiGdbCursor cur{vw, rec};
            for (uint32_t pc : kPhysicsComponents) {
                MultiGdbCursor owner;
                uint32_t comp_hash = 0;
                if (!MultiFindInherited(views, cur, pc, 6, owner,
                                        comp_hash) ||
                    comp_hash == 0 || comp_hash == kHashNull) {
                    continue;
                }
                MultiGdbCursor comp;
                if (!MultiLookup(views, comp_hash, comp)) continue;
                info.comp_field_hash = pc;
                info.comp_template_hash = comp_hash;
                MultiGdbCursor pf_owner;
                uint32_t pf_hash = 0;
                uint8_t pf_type = 0;
                if (MultiFindInherited(views, comp, kHashPhysicsFile, 0xFF,
                                       pf_owner, pf_hash, &pf_type) &&
                    (pf_type == 4 || pf_type == 7) &&
                    pf_hash != 0 && pf_hash != kHashNull) {
                    info.physics_file_hash = pf_hash;
                }
                break;
            }
            if (info.comp_field_hash == 0) continue;

            {
                constexpr uint32_t kHashTextTags = 0x709D872Bu;
                constexpr uint32_t kHashReadableComponent = 0x89ABB47Eu;
                MultiGdbCursor tt_owner;
                uint32_t tt_hash = 0;
                MultiGdbCursor cur2{vw, rec};
                if ((MultiFindInherited(views, cur2,
                                        kHashReadableComponent, 6,
                                        tt_owner, tt_hash) ||
                     MultiFindInherited(views, cur2, kHashTextTags, 6,
                                        tt_owner, tt_hash)) &&
                    tt_hash != 0 && tt_hash != kHashNull) {
                    info.has_text_tags = true;
                }
            }

            for (uint32_t mh : model_hashes) {
                auto it = out.find(mh);
                if (it == out.end()) {
                    out.emplace(mh, info);
                } else if (it->second.physics_file_hash == 0 &&
                           info.physics_file_hash != 0) {
                    it->second = info;
                }
            }
        }
    }
    return out;
}
