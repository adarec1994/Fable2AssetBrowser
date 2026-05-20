#include "Locomotion.h"

#include "../ISO/IsoMount.h"
#include "../UI/OutputLog.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <unordered_set>
#include <utility>

namespace Anim {

namespace {

constexpr uint32_t kMagic = 0xF007DA7Au;
constexpr uint32_t kVersion = 7u;
constexpr uint32_t kStateStride = 656u;
constexpr uint32_t kClipStride = 400u;
constexpr uint32_t kPtrStride = 4u;

uint32_t read_u32_be(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}

std::string table_string(const uint8_t* table, size_t table_size,
                         uint32_t offset) {
    if (offset == 0xFFFFFFFFu || offset >= table_size) return {};
    size_t end = offset;
    while (end < table_size && table[end] != 0) ++end;
    return std::string(reinterpret_cast<const char*>(table + offset),
                       end - offset);
}

void log_summary(const LocomotionSummary& summary) {
    std::unordered_set<std::string> unique_clip_names;
    unique_clip_names.reserve(summary.clips.size());
    for (const auto& clip : summary.clips) {
        if (!clip.name.empty()) unique_clip_names.insert(clip.name);
    }

    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "Locomotion: parsed %u state(s), %u clip record(s), "
                  "%zu unique clip name(s), %u ptr(s), %u string bytes",
                  summary.state_count, summary.clip_count,
                  unique_clip_names.size(), summary.ptr_count,
                  summary.string_size);
    OutputLog::success(buf);

    size_t shown = 0;
    for (const auto& st : summary.states) {
        if (st.primary_anim.empty()) continue;
        OutputLog::info("  loco state anim: " + st.primary_anim);
        if (++shown >= 3) break;
    }
}

}

bool load_locomotion_bytes(const uint8_t* data, size_t size,
                           LocomotionSummary* out_summary) {
    if (out_summary) *out_summary = {};
    if (!data || size < 40) {
        OutputLog::warn("Locomotion: file too short");
        return false;
    }

    const uint32_t magic = read_u32_be(data + 0);
    const uint32_t version = read_u32_be(data + 4);
    const uint32_t state_stride = read_u32_be(data + 8);
    const uint32_t clip_stride = read_u32_be(data + 12);
    const uint32_t ptr_stride = read_u32_be(data + 16);
    const uint32_t state_count = read_u32_be(data + 20);
    const uint32_t clip_count = read_u32_be(data + 24);
    const uint32_t ptr_count = read_u32_be(data + 28);
    const uint32_t string_size = read_u32_be(data + 32);
    const uint32_t body_size = read_u32_be(data + 36);

    if (magic != kMagic || version != kVersion ||
        state_stride != kStateStride || clip_stride != kClipStride ||
        ptr_stride != kPtrStride) {
        char buf[160];
        std::snprintf(buf, sizeof(buf),
                      "Locomotion: unsupported header magic=0x%08X ver=%u "
                      "strides=%u/%u/%u",
                      magic, version, state_stride, clip_stride, ptr_stride);
        OutputLog::warn(buf);
        return false;
    }

    const size_t state_base = 40;
    const size_t clip_base = state_base + (size_t)state_count * state_stride;
    const size_t ptr_base = clip_base + (size_t)clip_count * clip_stride;
    const size_t string_base = ptr_base + (size_t)ptr_count * ptr_stride;
    const size_t expected_end = string_base + string_size;
    if (body_size + 40 > size || expected_end > size) {
        OutputLog::warn("Locomotion: table extents exceed file size");
        return false;
    }

    const uint8_t* strings = data + string_base;
    LocomotionSummary summary;
    summary.state_count = state_count;
    summary.clip_count = clip_count;
    summary.ptr_count = ptr_count;
    summary.string_size = string_size;
    summary.states.reserve(state_count);
    summary.clips.reserve(clip_count);

    for (uint32_t i = 0; i < state_count; ++i) {
        const uint8_t* rec = data + state_base + (size_t)i * state_stride;
        LocomotionStateRecord st;
        st.hash = read_u32_be(rec + 0);
        st.primary_anim = table_string(strings, string_size,
                                       read_u32_be(rec + 20));
        st.secondary_anim = table_string(strings, string_size,
                                         read_u32_be(rec + 24));
        summary.states.push_back(std::move(st));
    }

    for (uint32_t i = 0; i < clip_count; ++i) {
        const uint8_t* rec = data + clip_base + (size_t)i * clip_stride;
        LocomotionClipRecord clip;
        clip.name = table_string(strings, string_size, read_u32_be(rec + 0));
        summary.clips.push_back(std::move(clip));
    }

    log_summary(summary);
    if (out_summary) *out_summary = std::move(summary);
    return true;
}

bool load_locomotion(const std::filesystem::path& path,
                     LocomotionSummary* out_summary) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        OutputLog::warn("Locomotion: cannot open " + path.string());
        return false;
    }
    std::streamsize sz = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes((size_t)sz);
    if (!f.read(reinterpret_cast<char*>(bytes.data()), sz)) {
        OutputLog::warn("Locomotion: read failed for " + path.string());
        return false;
    }
    return load_locomotion_bytes(bytes.data(), bytes.size(), out_summary);
}

bool load_locomotion_for_root(const std::string& root,
                              LocomotionSummary* out_summary) {
    constexpr const char* kRel = "data/entity/locomotion.loco";

    if (ISO::IsoMount::instance().is_mounted()) {
        const ISO::MountedFile* mf = ISO::IsoMount::instance().find(kRel);
        if (!mf) {
            mf = ISO::IsoMount::instance().find_by_basename("locomotion.loco");
        }
        if (!mf) {
            OutputLog::warn("Locomotion: locomotion.loco not found in ISO");
            return false;
        }
        auto bytes = ISO::IsoMount::instance().read_file(mf->path);
        if (bytes.empty()) {
            OutputLog::warn("Locomotion: failed to read " + mf->path);
            return false;
        }
        return load_locomotion_bytes(bytes.data(), bytes.size(), out_summary);
    }

    std::filesystem::path direct = std::filesystem::path(root) / kRel;
    if (std::filesystem::exists(direct)) {
        return load_locomotion(direct, out_summary);
    }

    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(
             root, std::filesystem::directory_options::skip_permission_denied, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); ++it) {
        if (it->is_regular_file(ec) &&
            it->path().filename() == "locomotion.loco") {
            return load_locomotion(it->path(), out_summary);
        }
    }

    OutputLog::warn("Locomotion: locomotion.loco not found under " + root);
    return false;
}

}
