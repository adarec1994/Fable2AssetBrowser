#pragma once

#include "AnimBank.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Anim {

class AnimDataFile {
public:

    bool open(const std::filesystem::path& data_path);

    bool open_from_bytes(std::vector<uint8_t> bytes);

    bool open_for_root(const std::string& root);

    void close();
    bool is_open() const { return !blob_.empty(); }
    size_t size() const  { return blob_.size(); }

    struct Span {
        const uint8_t* data;
        size_t         size;
        bool empty() const { return data == nullptr || size == 0; }
    };
    Span clip_bytes(const AnimClip& clip) const;

    struct ClipHeader {
        uint32_t magic            = 0;
        uint32_t version          = 0;
        uint32_t field_8          = 0;
        uint32_t field_C          = 0;  // raw +0x0C: decoded track record count
        uint32_t track_count      = 0;
        uint32_t bone_count       = 0;
        uint32_t frame_count      = 0;
        uint32_t bone_idx_bits    = 0;
        size_t   packed_body_offset = 0;
        std::vector<uint32_t> bone_offsets;
        bool ok = false;
    };
    ClipHeader parse_clip_header(const AnimClip& clip) const;

    bool header_ok() const;

private:
    std::vector<uint8_t> blob_;
};

AnimDataFile& global_data_file();

}
