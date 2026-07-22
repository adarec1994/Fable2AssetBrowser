#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Level {




struct TerrainLightmap {
    bool                 ok = false;
    uint32_t             file_version = 0;
    uint32_t             version12_word = 0;
    uint32_t             static_record_count = 0;
    uint32_t             variant_record_count = 0;
    uint32_t             word_record_count = 0;
    uint32_t             fixed_record_count = 0;
    uint64_t             key = 0;
    uint32_t             sample_width = 0;
    uint32_t             sample_height = 0;
    uint32_t             texture_width = 0;
    uint32_t             texture_height = 0;
    uint32_t             pixel_format = 0;
    uint32_t             storage_flags = 0;
    std::vector<uint8_t> rgba;
    std::string          error;
};



bool DecodeTerrainLightmap(const std::vector<uint8_t>& gzip_bytes,
                           uint64_t key,
                           TerrainLightmap& out);





bool AttachTerrainLightmapRecord(const std::vector<uint8_t>& existing_gzip,
                                 const std::vector<uint8_t>& donor_gzip,
                                 uint64_t key,
                                 std::vector<uint8_t>& out_gzip,
                                 std::string& error);

}
