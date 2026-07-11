#pragma once


#include <array>
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
    uint32_t    name_hash = 0;
    uint8_t     type = 0;
    // Serialized program ordinal.  The retail loader divides this by four
    // to select a compressed group and uses the remainder as its slot.
    uint32_t    program_ordinal = 0;
};

struct BindingDescriptor {
    uint32_t logical_id = 0;
    uint32_t hardware_binding = 0;
    uint32_t count = 0;
    // Copied verbatim into the runtime descriptor; semantic meaning is not
    // established by the cloud setter paths inspected in default.xex.
    uint32_t tag = 0;
};

struct Program {
    uint32_t             group_index = 0;
    uint32_t             group_slot = 0;
    uint32_t             record_offset = 0;
    std::vector<uint8_t> record;
    std::array<std::vector<BindingDescriptor>, 2> binding_lists;

    // The final length-prefixed block consumed by sub_82B843F8/
    // sub_82B84550.  It contains the Xbox shader header plus Xenos ucode.
    std::vector<uint8_t> compiled_shader;
    uint32_t             compiled_flags = 0;
    uint32_t             compiled_header_size = 0;
    std::vector<uint8_t> xenos_ucode;
};

struct ProgramGroup {
    uint32_t             blob_offset = 0;
    uint32_t             compressed_size = 0;
    uint32_t             decompressed_size = 0;
    std::vector<uint8_t> decompressed;
};

struct Bank {
    bool        ok = false;
    std::string error;
    uint32_t    version = 0;
    uint8_t     stores_shader_names = 0;
    uint32_t    bank_hash = 0;

    std::vector<ParamEntry>  params;
    std::vector<std::string> resources;
    std::vector<ShaderEntry> shaders;
    std::vector<ProgramGroup> program_groups;
    std::vector<Program>     programs;
};

bool ParseShaderBank(const std::vector<uint8_t>& data, Bank& out);

}
