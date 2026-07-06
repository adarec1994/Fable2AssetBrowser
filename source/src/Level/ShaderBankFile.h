#pragma once


#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ShaderBank {

struct ParamEntry {
    uint32_t hdr[9] = {};
    std::vector<std::pair<std::string, uint32_t>> subs;
};

struct ShaderEntry {
    std::string name;
    uint8_t     type = 0;
    uint32_t    parent = 0;
};

struct Program {
    uint32_t             offset = 0;
    uint32_t             decompressed_size = 0;
    std::vector<uint8_t> microcode;
};

struct Bank {
    bool        ok = false;
    std::string error;
    uint32_t    version = 0;
    uint8_t     endian  = 0;
    uint32_t    bank_hash = 0;

    std::vector<ParamEntry>  params;
    std::vector<std::string> resources;
    std::vector<ShaderEntry> shaders;
    std::vector<Program>     programs;
};

bool ParseShaderBank(const std::vector<uint8_t>& data, Bank& out);

}
