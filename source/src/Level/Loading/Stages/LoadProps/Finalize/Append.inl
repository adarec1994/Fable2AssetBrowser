            size_t extra_blocks = 0, extra_insts = 0;
            {
                std::vector<Level::PropBlock> derived_bridge_blocks;
                for (const auto& kv : blocks_by_path) {
                    if (is_bridge_debug_path(kv.second.model_path) ||
                        is_bridge_debug_path(kv.second.lod_model_path) ||
                        is_bridge_debug_path(kv.second.shadow_model_path) ||
                        is_bridge_debug_path(kv.second.extra_model_path)) {
                        derived_bridge_blocks.push_back(kv.second);
                    }
                }
                bridge_debug_dump_blocks(
                    "DERIVED BLOCKS BEFORE PROP PIPELINE",
                    derived_bridge_blocks);
            }
            for (auto& kv : blocks_by_path) {
                if (kv.second.instances.empty()) continue;
                ++extra_blocks;
                extra_insts += kv.second.instances.size();
                g_pending_level_prop_blocks.push_back(std::move(kv.second));
            }
            std::ostringstream eos;
            eos << "derived placements: "
                << extra_blocks << " unique models / "
                << extra_insts << " instances appended to prop pipeline";
            if (extra_insts > 0) OutputLog::success(eos.str());
            else                 OutputLog::warn(eos.str());
        } else {
            OutputLog::warn("no .gdb sibling in BNK");
        }
