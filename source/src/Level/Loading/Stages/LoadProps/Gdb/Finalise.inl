            if (gdb_instances_emitted > 0 &&
                (!gdb_dup_slot_offsets.empty() ||
                 !gdb_pos_slot_links.empty())) {
                auto link_block_instances = [&](Level::PropBlock& block) {
                    if (block.model_path.empty()) return;
                    for (auto& inst : block.instances) {
                        auto pos_hit = gdb_pos_slot_links.find(
                            gdb_pos_link_key(block.model_path,
                                             inst.values[0],
                                             inst.values[1],
                                             inst.values[2]));
                        const bool have_pos_link =
                            pos_hit != gdb_pos_slot_links.end();
                        if (inst.gdb_pos_off[0] || inst.gdb_pos_off[1] ||
                            inst.gdb_pos_off[2]) {
                            if (have_pos_link && inst.gdb_entity_hash == 0) {
                                inst.gdb_entity_hash =
                                    pos_hit->second.entity_hash;
                            }
                            continue;
                        }
                        auto hit = gdb_dup_slot_offsets.find(
                            prop_instance_transform_key(
                                inst, block.model_path));
                        if (hit != gdb_dup_slot_offsets.end()) {
                            inst.gdb_pos_off[0] = hit->second[0];
                            inst.gdb_pos_off[1] = hit->second[1];
                            inst.gdb_pos_off[2] = hit->second[2];
                            inst.gdb_rot_off[0] = hit->second[3];
                            inst.gdb_rot_off[1] = hit->second[4];
                            inst.gdb_rot_off[2] = hit->second[5];
                        } else if (have_pos_link) {
                            inst.gdb_pos_off[0] = pos_hit->second.slots[0];
                            inst.gdb_pos_off[1] = pos_hit->second.slots[1];
                            inst.gdb_pos_off[2] = pos_hit->second.slots[2];
                            inst.gdb_rot_off[0] = pos_hit->second.slots[3];
                            inst.gdb_rot_off[1] = pos_hit->second.slots[4];
                            inst.gdb_rot_off[2] = pos_hit->second.slots[5];
                        } else {
                            continue;
                        }
                        if (have_pos_link && inst.gdb_entity_hash == 0) {
                            inst.gdb_entity_hash =
                                pos_hit->second.entity_hash;
                        }
                    }
                };
                for (auto& block : level_prop_blocks) {
                    link_block_instances(block);
                }
                for (auto& kv : blocks_by_path) {
                    link_block_instances(kv.second);
                }
            }
