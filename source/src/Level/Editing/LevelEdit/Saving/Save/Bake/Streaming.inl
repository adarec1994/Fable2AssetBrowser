                                std::string aerr;
                                if (!BnkWriter::AddEntriesToBnkBytes(
                                        models_blob, mdl_adds, aerr)) {
                                    DebugTrace::log(
                                        "save: models bank add failed: "
                                        "%s", aerr.c_str());
                                } else {
                                    bake_models_index = models_idx;
                                    bake_models_bytes =
                                        std::move(models_blob);
                                    bake_streaming_path = streaming_path;
                                }
                                if (bake_models_index >= 0 &&
                                    !stream_adds.empty()) {
                                    const std::string lvs_key =
                                        stem + "_streaming.bnk";
                                    const int lvs_idx =
                                        BnkCache::find_index(
                                            s.lev.bnk_path, lvs_key);
                                    if (lvs_idx >= 0) {
                                        std::vector<uint8_t> lvs =
                                            BnkCache::extract_bytes(
                                                s.lev.bnk_path, lvs_idx);
                                        std::string serr;
                                        std::vector<
                                            BnkWriter::EntryAddition>
                                            fresh;
                                        try {
                                            BNKReader lr(lvs);
                                            for (auto& sa : stream_adds) {
                                                if (!nested_bank_has(
                                                        lr,
                                                        norm_key(
                                                            sa.name))) {
                                                    fresh.push_back(
                                                        std::move(sa));
                                                }
                                            }
                                        } catch (...) {}
                                        if (!fresh.empty() &&
                                            BnkWriter::
                                                AddEntriesToBnkBytes(
                                                    lvs, fresh, serr)) {
                                            bake_lvstream_index = lvs_idx;
                                            bake_lvstream_bytes =
                                                std::move(lvs);
                                        } else if (!fresh.empty()) {
                                            DebugTrace::log(
                                                "save: level streaming "
                                                "add failed: %s",
                                                serr.c_str());
                                        }
                                    }
                                }
                            }
                        }
                    } catch (const std::exception& ex) {
                        bake_ed_index = -1;
                        bake_ed_bytes.clear();
                        bake_lmp_index = -1;
                        bake_lmp_bytes.clear();
                        DebugTrace::log(
                            "save: engine_data/lmp patch failed: %s",
                            ex.what());
                    } catch (...) {
                        bake_ed_index = -1;
                        bake_ed_bytes.clear();
                        bake_lmp_index = -1;
                        bake_lmp_bytes.clear();
                    }
                }
                s.saving = true;
            }
        } else {
            if (!level_placement_deletes.empty()) {
                msg = "save failed: this level entry cannot be rewritten "
                      "to remove render placements";
                return false;
            }
            bake_note = std::string("; placed model(s) kept app-side "
                                    "(level BNK not rewritable: ") +
                        (s.lev.compressed ? "chunk-compressed"
                                          : "no locator") + ")";
        }
        if (!need_bake) {
            DebugTrace::log("save: bake not started:%s",
                            bake_note.c_str());
        }
    }
