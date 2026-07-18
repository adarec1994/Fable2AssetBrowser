bool load_toc_bytes(const uint8_t* data, size_t size,
                    std::vector<AnimClip>& out_clips) {
    out_clips.clear();
    if (!data || size == 0) {
        OutputLog::error("AnimBank: empty input");
        return false;
    }

    Reader r;
    r.base = data;
    r.size = size;

    if (!r.need(8)) {
        OutputLog::error("AnimBank: file too short");
        return false;
    }
    if (std::memcmp(r.base + r.pos, "AnimBank", 8) != 0) {
        OutputLog::error("AnimBank: bad magic (not 'AnimBank')");
        return false;
    }
    r.pos += 8;

    uint32_t magic1            = r.u32();
    uint32_t magic2            = r.u32();
    uint32_t num_anim_records  = r.u32();
    uint32_t num_special       = r.u32();
    uint32_t num_strings       = r.u32();
    if (!r.ok) {
        OutputLog::error("AnimBank: header truncated");
        return false;
    }
    if (magic1 != 1 || magic2 != 5) {

        char buf[96];
        std::snprintf(buf, sizeof(buf),
                      "AnimBank: unsupported version %u/%u (expected 1/5)",
                      magic1, magic2);
        OutputLog::error(buf);
        return false;
    }

    std::vector<std::string> strings;
    strings.reserve(num_strings);
    for (uint32_t i = 0; i < num_strings; ++i) {
        strings.push_back(r.cstr());
        if (!r.ok) {
            OutputLog::error("AnimBank: string table truncated at #" +
                             std::to_string(i));
            return false;
        }
    }

    out_clips.reserve(num_anim_records);
    for (uint32_t i = 0; i < num_anim_records; ++i) {
        AnimClip c;
        c.key0            = r.u32();
        c.key1            = r.u32();
        c.data_offset     = r.u32();
        c.toc_frame_count = r.u32();
        c.data_length     = c.toc_frame_count;
        c.fps             = r.f32();
        uint32_t n_events = r.u32();
        if (!r.ok) {
            OutputLog::error("AnimBank: record header truncated at #" +
                             std::to_string(i));
            return false;
        }

        c.events.reserve(n_events);
        for (uint32_t e = 0; e < n_events; ++e) {
            AnimEvent ev;
            ev.time                = r.f32();
            uint32_t name_idx      = r.u32();
            uint32_t param_idx     = r.u32();
            ev.name  = lookup(name_idx,  strings);
            ev.param = lookup(param_idx, strings);
            c.events.push_back(std::move(ev));
        }
        if (!r.ok) {
            OutputLog::error("AnimBank: events truncated at clip #" +
                             std::to_string(i));
            return false;
        }

        char nbuf[16];
        std::snprintf(nbuf, sizeof(nbuf), "id_%08X", c.key0);
        c.name = nbuf;

        out_clips.push_back(std::move(c));
    }

    std::vector<size_t> order(out_clips.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(),
              [&](size_t a, size_t b) {
                  return out_clips[a].data_offset < out_clips[b].data_offset;
              });
    for (size_t i = 0; i < order.size(); ++i) {
        AnimClip& c = out_clips[order[i]];
        if (i + 1 < order.size()) {
            const uint32_t next = out_clips[order[i + 1]].data_offset;
            c.data_size_bytes = next > c.data_offset
                ? (next - c.data_offset)
                : 0;
        } else {
            c.data_size_bytes = 0;
        }
    }

    std::unordered_map<uint32_t,
                       std::shared_ptr<const std::vector<AnimTrackBone>>> track_maps;
    track_maps.reserve(num_special);

    for (uint32_t i = 0; i < num_special; ++i) {
        uint32_t header[8] = {};
        for (uint32_t h = 0; h < 8; ++h) {
            header[h] = r.u32();
        }
        if (!r.ok) {
            OutputLog::warn("AnimBank: special-record header truncated at #" +
                            std::to_string(i) + " (clips already parsed)");
            r.ok = true;
            break;
        }
        uint32_t n_args = r.u32();
        auto bones = std::make_shared<std::vector<AnimTrackBone>>();
        bones->reserve(n_args);
        for (uint32_t a = 0; a < n_args; ++a) {
            const uint32_t name_idx = r.u32();
            const uint32_t parent_idx = r.u32();
            AnimTrackBone tb;
            tb.name = lookup(name_idx, strings);
            tb.parent = (parent_idx == 0xFFFFFFFFu)
                ? -1
                : (int32_t)parent_idx;
            bones->push_back(std::move(tb));
        }
        if (!r.ok) {
            OutputLog::warn("AnimBank: special-record args truncated at #" +
                            std::to_string(i) + " (clips already parsed)");
            r.ok = true;
            break;
        }

        const uint32_t track_map_key = header[6];
        track_maps[track_map_key] = bones;
    }

    size_t mapped_clips = 0;
    for (auto& c : out_clips) {
        auto it = track_maps.find(c.key1);
        if (it == track_maps.end()) continue;
        c.track_map = it->second;
        ++mapped_clips;
    }

    OutputLog::success("AnimBank: parsed " +
                       std::to_string(out_clips.size()) +
                       " clip(s), " +
                       std::to_string(strings.size()) + " string(s), " +
                       std::to_string(track_maps.size()) + " track map(s), " +
                       std::to_string(mapped_clips) + " clip map link(s)");
    return true;
}
