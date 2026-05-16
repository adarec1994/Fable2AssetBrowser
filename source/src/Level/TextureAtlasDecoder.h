#pragma once

#include <cstdint>
#include <string>
#include <vector>


namespace TextureAtlas {

struct DecodedAtlas {
    bool                  ok          = false;
    std::vector<uint8_t>  rgba;        
    int                   width       = 0;
    int                   height      = 0;
    uint32_t              pixel_format = 0;  
    std::string           error;       
};

DecodedAtlas DecodeAtlas(const std::vector<uint8_t>& blob);

bool DecodeZlibBc1Page(const uint8_t* zlib_stream,
                       size_t          comp_size,
                       size_t          expected_raw,
                       int             width_pixels,
                       int             height_pixels,
                       std::vector<uint8_t>& rgba);

bool DecodeZlibBc3Page(const uint8_t* zlib_stream,
                       size_t          comp_size,
                       size_t          expected_raw,
                       int             width_pixels,
                       int             height_pixels,
                       std::vector<uint8_t>& rgba);

bool DecodeZlibBc5Page(const uint8_t* zlib_stream,
                       size_t          comp_size,
                       size_t          expected_raw,
                       int             width_pixels,
                       int             height_pixels,
                       std::vector<uint8_t>& rgba);

bool DecodeRawBc1ToRgba(const uint8_t* bc1, size_t bc1_size,
                        int W, int H, std::vector<uint8_t>& rgba);

bool DecodePF99SplatMap(const uint8_t* pf99_blob, size_t blob_size,
                        std::vector<uint8_t>& out_indices,
                        int& out_w, int& out_h,
                        std::string& out_err);

}  
