#pragma once

// Byte-identical parser for Fable 2's ShaderBankFile (.sbk / ShadersRelease.sbk).
// Reversed from the XEX loader chain (sub_82B7CD18 -> sub_82B7C488/sub_82B7BCB0,
// sub_82B7C5A8, sub_82B7C858/sub_82B8A758) and validated byte-for-byte against
// data\Shaders\Shaders.sbk (147/147 program microcode blobs decompress exactly;
// blob 43 = the LANDSCAPEMATERIAL_*_CLF1 group). All multi-byte fields big-endian.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ShaderBank {

// One entry of the global parameter table (9 u32 header + named sub-params).
struct ParamEntry {
    uint32_t hdr[9] = {};
    std::vector<std::pair<std::string, uint32_t>> subs;   // {name, value}
};

// One shader permutation (name + type + parent/derived index).
struct ShaderEntry {
    std::string name;      // e.g. "PSHADER_LANDSCAPEMATERIAL_BLT7_GLT1_MIST1_CLF1"
    uint8_t     type = 0;  // 1 = pixel shader, 2 = vertex shader
    uint32_t    parent = 0;
};

// One decompressed microcode program blob (permutation group).
struct Program {
    uint32_t             offset = 0;             // start within the compressed blob
    uint32_t             decompressed_size = 0;  // expected size from the dsize[] table
    std::vector<uint8_t> microcode;              // decompressed permutation-group bytes
};

struct Bank {
    bool        ok = false;
    std::string error;
    uint32_t    version = 0;    // == 3
    uint8_t     endian  = 0;    // endian flag byte
    uint32_t    bank_hash = 0;

    std::vector<ParamEntry>  params;      // global parameter table
    std::vector<std::string> resources;   // SHADER_RESOURCE_BINDING_* names
    std::vector<ShaderEntry> shaders;     // per-permutation entries
    std::vector<Program>     programs;     // 147 microcode blobs
};

// Parse a whole .sbk buffer. Returns false (with Bank::error set) on any
// inconsistency. Program microcode is decompressed in place.
bool ParseShaderBank(const std::vector<uint8_t>& data, Bank& out);

}  // namespace ShaderBank
