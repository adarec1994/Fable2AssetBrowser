                            const std::filesystem::path data_dir = bake_iso
                                ? std::filesystem::path(bake_vpath)
                                      .parent_path()
                                : std::filesystem::path(s.lev.bnk_path)
                                      .parent_path();
                            const std::string streaming_path = bake_iso
                                ? ISO::IsoMount::make_iso_path(
                                      (data_dir / "streaming.bnk")
                                          .generic_string())
                                : (data_dir / "streaming.bnk").string();
                            const std::string globals_path = bake_iso
                                ? ISO::IsoMount::make_iso_path(
                                      (data_dir / "Globals" /
                                       "globals_models.bnk")
                                          .generic_string())
                                : (data_dir / "Globals" /
                                   "globals_models.bnk").string();
                            const std::string models_key =
                                stem + "_models.bnk";
                            std::vector<BnkWriter::EntryAddition>
                                mdl_adds;
                            std::vector<BnkWriter::EntryAddition>
                                stream_adds;
                            const bool streaming_exists = bake_iso
                                ? ISO::IsoMount::instance().find(
                                      ISO::IsoMount::strip_iso_prefix(
                                          streaming_path)) != nullptr
                                : std::filesystem::exists(streaming_path);
                            const int models_idx = streaming_exists
                                ? BnkCache::find_index(streaming_path,
                                                       models_key)
                                : -1;
                            std::vector<uint8_t> models_blob;
                            if (models_idx >= 0) {
                                models_blob = BnkCache::extract_bytes(
                                    streaming_path, models_idx);
                            }
                            std::vector<std::string> inject_paths;
                            for (const auto& a : s.additions) {
                                if (a.removed) continue;
                                inject_paths.push_back(a.model_path);
                            }
                            for (const auto& mp : gen_asset_models) {
                                inject_paths.push_back(mp);
                            }
                            for (const auto& inj_path : inject_paths) {
                                const std::string lp =
                                    lower_model_path(inj_path);
                                const std::string want = norm_key(lp);
                                bool have = false;
                                if (!models_blob.empty()) {
                                    try {
                                        BNKReader lm(models_blob);
                                        have = nested_bank_has(lm, want);
                                    } catch (...) {}
                                }
                                if (!have &&
                                    BnkCache::find_index(globals_path,
                                                         want) >= 0) {
                                    have = true;
                                }
                                bool queued = false;
                                for (const auto& q : mdl_adds) {
                                    if (norm_key(q.name) == want) {
                                        queued = true;
                                        break;
                                    }
                                }
                                if (have || queued) continue;
                                std::string src_name;
                                std::vector<uint8_t> src_payload;
                                if (models_idx < 0 ||
                                    !find_in_nested_banks(
                                        streaming_path, "_models.bnk",
                                        want, src_name, src_payload)) {
                                    continue;
                                }
                                mdl_adds.push_back(
                                    {src_name, std::move(src_payload)});
                                const size_t slash = want.rfind('/');
                                if (slash != std::string::npos) {
                                    const std::string folder =
                                        want.substr(0, slash + 1);
                                    size_t got =
                                        collect_folder_from_nested_banks(
                                            s.lev.bnk_path,
                                            "_streaming.bnk", folder,
                                            stream_adds);
                                    if (got == 0) {
                                        for (const auto& other :
                                             S.bnk_paths) {
                                            if (other == s.lev.bnk_path) {
                                                continue;
                                            }
                                            got =
                                            collect_folder_from_nested_banks(
                                                other, "_streaming.bnk",
                                                folder, stream_adds);
                                            if (got) {
                                                break;
                                            }
                                        }
                                    }
                                }
                            }
                            if (!mdl_adds.empty()) {
                                const std::string scen_dir =
                                    stem.substr(0, stem.rfind('/') + 1);
                                const std::string hdrs_key =
                                    stem + "_texture_headers.bnk";
                                const std::string body_key =
                                    scen_dir + "textures.bnk";
                                const std::string mani_key =
                                    body_key + ".manifest";
                                const int hdrs_idx = BnkCache::find_index(
                                    s.lev.bnk_path, hdrs_key);
                                const int body_idx = BnkCache::find_index(
                                    s.lev.bnk_path, body_key);
                                const int mani_idx = BnkCache::find_index(
                                    s.lev.bnk_path, mani_key);
                                std::unordered_set<std::string> have_hdr;
                                std::unordered_set<std::string> have_body;
                                std::vector<uint8_t> hdrs_blob, body_blob;
                                if (hdrs_idx >= 0) {
                                    hdrs_blob = BnkCache::extract_bytes(
                                        s.lev.bnk_path, hdrs_idx);
                                    try {
                                        BNKReader r(hdrs_blob);
                                        for (const auto& fe :
                                             r.list_files())
                                            have_hdr.insert(
                                                norm_key(fe.name));
                                    } catch (...) {}
                                }
                                if (body_idx >= 0) {
                                    body_blob = BnkCache::extract_bytes(
                                        s.lev.bnk_path, body_idx);
                                    try {
                                        BNKReader r(body_blob);
                                        for (const auto& fe :
                                             r.list_files())
                                            have_body.insert(
                                                norm_key(fe.name));
                                    } catch (...) {}
                                }
                                {
                                    const int sh_idx =
                                        BnkCache::find_index(
                                            s.lev.bnk_path,
                                            "worlds/albion/shared/"
                                            "shared_6281.bnk");
                                    if (sh_idx >= 0) {
                                        try {
                                            std::vector<uint8_t> sh =
                                                BnkCache::extract_bytes(
                                                    s.lev.bnk_path,
                                                    sh_idx);
                                            BNKReader r(sh);
                                            for (const auto& fe :
                                                 r.list_files())
                                                have_body.insert(
                                                    norm_key(fe.name));
                                        } catch (...) {}
                                    }
                                }
                                std::vector<BnkWriter::EntryAddition>
                                    hdr_adds, body_adds;
                                std::string mani_append;
                                for (const auto& ma : mdl_adds) {
                                    std::vector<std::string> texs;
                                    collect_tex_refs(ma.payload, texs);
                                    for (const auto& t : texs) {
                                        if (!have_hdr.count(t)) {
                                            std::string sn;
                                            std::vector<uint8_t> sp;
                                            if (find_in_nested_banks(
                                                    s.lev.bnk_path,
                                                    "_texture_headers"
                                                    ".bnk",
                                                    t, sn, sp)) {
                                                hdr_adds.push_back(
                                                    {sn,
                                                     std::move(sp)});
                                                have_hdr.insert(t);
                                            } else {
                                            }
                                        }
                                        if (!have_body.count(t)) {
                                            std::string sn;
                                            std::vector<uint8_t> sp;
                                            if (find_in_nested_banks(
                                                    s.lev.bnk_path,
                                                    "/textures.bnk", t,
                                                    sn, sp)) {
                                                body_adds.push_back(
                                                    {sn,
                                                     std::move(sp)});
                                                have_body.insert(t);
                                                std::string tl = t;
                                                std::replace(tl.begin(),
                                                             tl.end(),
                                                             '/', '\\');
                                                mani_append +=
                                                    "\"" + tl + "\" \"" +
                                                    tl + "\" 0 0 3\r\n";
                                            } else {
                                            }
                                        }
                                    }
                                }
                                std::string terr;
                                if (!hdr_adds.empty() &&
                                    hdrs_idx >= 0 &&
                                    BnkWriter::AddEntriesToBnkBytes(
                                        hdrs_blob, hdr_adds, terr)) {
                                    BnkWriter::EntryReplacement r;
                                    r.file_index = hdrs_idx;
                                    r.payload = std::move(hdrs_blob);
                                    bake_more.push_back(std::move(r));
                                } else if (!hdr_adds.empty()) {
                                }
                                if (!body_adds.empty() &&
                                    body_idx >= 0 &&
                                    BnkWriter::AddEntriesToBnkBytes(
                                        body_blob, body_adds, terr)) {
                                    BnkWriter::EntryReplacement r;
                                    r.file_index = body_idx;
                                    r.payload = std::move(body_blob);
                                    bake_more.push_back(std::move(r));
                                    if (mani_idx >= 0 &&
                                        !mani_append.empty()) {
                                        std::vector<uint8_t> mani =
                                            BnkCache::extract_bytes(
                                                s.lev.bnk_path,
                                                mani_idx);
                                        const bool crlf =
                                            std::find(mani.begin(),
                                                      mani.end(),
                                                      (uint8_t)'\r') !=
                                            mani.end();
                                        if (!crlf) {
                                            std::string tmp;
                                            for (char c : mani_append)
                                                if (c != '\r')
                                                    tmp.push_back(c);
                                            mani_append = tmp;
                                        }
                                        mani.insert(mani.end(),
                                                    mani_append.begin(),
                                                    mani_append.end());
                                        BnkWriter::EntryReplacement r2;
                                        r2.file_index = mani_idx;
                                        r2.payload = std::move(mani);
                                        bake_more.push_back(
                                            std::move(r2));
                                    }
                                } else if (!body_adds.empty()) {
                                }
