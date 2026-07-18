bool RunLevProbe(const std::string& bnk_path, std::string& msg) {
    return RunLevProbeMode(bnk_path, false, msg);
}

bool RunLevProbeMode(const std::string& bnk_path, bool float_only,
                     std::string& msg) {
    const std::string lev_key =
        "worlds/albion/bwsslums/defaultscenario/"
        "defaultscenario.engine_level";
    const std::string lmp_key =
        "worlds/albion/bwsslums/defaultscenario/defaultscenario.lmp";
    const int lev_idx = BnkCache::find_index(bnk_path, lev_key);
    const int lmp_idx = BnkCache::find_index(bnk_path, lmp_key);
    if (lev_idx < 0 || lmp_idx < 0) {
        msg = "probe: entries not found in " + bnk_path;
        return false;
    }
    std::vector<uint8_t> lev;
    std::vector<uint8_t> lmp_gz;
    try {
        lev = BnkCache::extract_bytes(bnk_path, lev_idx);
        lmp_gz = BnkCache::extract_bytes(bnk_path, lmp_idx);
    } catch (const std::exception& ex) {
        msg = std::string("probe: extract failed: ") + ex.what();
        return false;
    }

    Level::EngineLevelInfo info;
    if (!Level::ParseEngineLevel(lev, info)) {
        msg = "probe: lev parse failed: " + info.error;
        return false;
    }
    const Level::PropBlock* oak = nullptr;
    for (const auto& b : info.prop_blocks) {
        if (b.type == 2 &&
            b.model_path.find("bs_snowyoak") != std::string::npos) {
            oak = &b;
            break;
        }
    }
    if (!oak || oak->instances.empty()) {
        msg = "probe: snowyoak block not found";
        return false;
    }
    const Level::PropInstance& inst = oak->instances[0];
    const size_t fo = inst.pos_file_offset;
    const size_t rec_start = fo - 11;
    const size_t cnt_off = rec_start - 4;
    const uint32_t count = get_u32_be2(lev.data() + cnt_off);
    if (count != oak->instances.size() || fo + 80 > lev.size()) {
        msg = "probe: block layout mismatch";
        return false;
    }

    float z = inst.values[2] + 8.0f;
    uint32_t zb;
    std::memcpy(&zb, &z, 4);
    lev[fo + 8]  = uint8_t(zb >> 24);
    lev[fo + 9]  = uint8_t(zb >> 16);
    lev[fo + 10] = uint8_t(zb >> 8);
    lev[fo + 11] = uint8_t(zb);

    const uint64_t clone_hash =
        ((uint64_t)fnv1_32(oak->model_path) << 32) |
        fnv1_32(oak->model_path + "#probe_clone");
    float vals[20];
    std::memcpy(vals, inst.values, sizeof(vals));
    if (!float_only) {
        std::vector<uint8_t> rec;
        rec.push_back(1);
        rec.push_back(0);
        rec.push_back(0);
        put_u64_be(rec, clone_hash);
        vals[0] += 4.0f;
        for (int k = 0; k < 20; ++k) put_f32_be_v(rec, vals[k]);
        const size_t insert_at = rec_start + (size_t)count * 91;
        lev.insert(lev.begin() + insert_at, rec.begin(), rec.end());
        const uint32_t nc = count + 1;
        lev[cnt_off]     = uint8_t(nc >> 24);
        lev[cnt_off + 1] = uint8_t(nc >> 16);
        lev[cnt_off + 2] = uint8_t(nc >> 8);
        lev[cnt_off + 3] = uint8_t(nc);
    }

    if (float_only) {
        BnkCache::invalidate(bnk_path);
        std::vector<BnkWriter::EntryReplacement> reps(1);
        reps[0].file_index = lev_idx;
        reps[0].payload = std::move(lev);
        std::string berr;
        if (!BnkWriter::RebuildWithReplacedEntries(bnk_path, reps,
                                                   berr)) {
            msg = "probe: rebuild failed: " + berr;
            return false;
        }
        BnkCache::invalidate(bnk_path);
        char buf[192];
        std::snprintf(buf, sizeof(buf),
                      "probe OK (float only): oak raised to z=%.2f at "
                      "(%.2f, %.2f)",
                      z, inst.values[0], inst.values[1]);
        msg = buf;
        DebugTrace::log("%s", buf);
        return true;
    }

    std::vector<uint8_t> raw;
    if (!gzip_inflate(lmp_gz, raw)) {
        msg = "probe: lmp gunzip failed";
        return false;
    }
    const size_t total = raw.size();
    size_t n = 0;
    for (size_t c = (total - 4) / 56; c >= 1; --c) {
        const size_t pos = total - 56 * c - 4;
        if (pos < 32) continue;
        if (get_u32_be2(raw.data() + pos) == (uint32_t)c) {
            n = c;
            break;
        }
    }
    if (!n) {
        msg = "probe: lmp probe section not found";
        return false;
    }
    const size_t sec = total - 56 * n;
    size_t donor = 0;
    for (size_t i = 0; i < n; ++i) {
        uint64_t h = 0;
        for (int k = 0; k < 8; ++k) {
            h = (h << 8) | raw[sec + i * 56 + (size_t)k];
        }
        if (h == inst.hash) {
            donor = sec + i * 56;
            break;
        }
    }
    if (!donor) {
        msg = "probe: snowyoak lmp record not found";
        return false;
    }
    std::vector<uint8_t> lrec;
    put_u64_be(lrec, clone_hash);
    lrec.insert(lrec.end(), raw.begin() + donor + 8,
                raw.begin() + donor + 56);
    raw.insert(raw.end(), lrec.begin(), lrec.end());
    const uint32_t nn = (uint32_t)(n + 1);
    raw[sec - 4] = uint8_t(nn >> 24);
    raw[sec - 3] = uint8_t(nn >> 16);
    raw[sec - 2] = uint8_t(nn >> 8);
    raw[sec - 1] = uint8_t(nn);
    std::vector<uint8_t> lmp_out;
    if (!gzip_deflate(raw, lmp_out)) {
        msg = "probe: lmp gzip failed";
        return false;
    }

    BnkCache::invalidate(bnk_path);
    std::vector<BnkWriter::EntryReplacement> reps(2);
    reps[0].file_index = lev_idx;
    reps[0].payload = std::move(lev);
    reps[1].file_index = lmp_idx;
    reps[1].payload = std::move(lmp_out);
    std::string berr;
    if (!BnkWriter::RebuildWithReplacedEntries(bnk_path, reps, berr)) {
        msg = "probe: rebuild failed: " + berr;
        return false;
    }
    BnkCache::invalidate(bnk_path);
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "probe OK: oak raised to z=%.2f at (%.2f, %.2f); clone "
                  "hash=%016llx at x=%.2f",
                  z, inst.values[0], inst.values[1],
                  (unsigned long long)clone_hash, vals[0]);
    msg = buf;
    DebugTrace::log("%s", buf);
    return true;
}
