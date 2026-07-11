#pragma once

#include "Level/ShaderBankFile.h"
#include "XenosShaderBinary.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace CloudShaderBank {

// Indices passed to the retail shader-bank selector by default.xex
// 0x8223A1E8.  They are shader-table entries, not packed-program ordinals.
constexpr std::uint32_t kVertexShaderEntry = 135;
constexpr std::uint32_t kPixelShaderEntryContext2 = 136;
constexpr std::uint32_t kPixelShaderEntryContext2Background = 137;
constexpr std::uint32_t kPixelShaderEntryOtherContext = 138;

// Generated-background pass selected by BgMap_InitResources 0x82A7A7A4.
// This pass produces the FMT_8 texture later sampled by cloud PS entry 137.
constexpr std::uint32_t kBackgroundMapVertexShaderEntry = 85;
constexpr std::uint32_t kBackgroundMapPixelShaderEntry = 86;

// Non-owning bytes from ShaderBank::Program.  The view remains valid while
// the source Bank and its vectors remain alive and unmodified.
struct ByteView {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
};

// A validated link from one retail shader-table entry to its packed program.
// compiled_shader includes the Xbox shader header; xenos_ucode points into the
// parser's exact extracted ucode bytes without translation or reconstruction.
struct ProgramView {
    std::uint32_t shader_entry = 0;
    std::uint32_t program_ordinal = 0;
    const ShaderBank::ShaderEntry* shader = nullptr;
    const ShaderBank::Program* program = nullptr;
    ByteView record{};
    ByteView compiled_shader{};
    ByteView xenos_ucode{};
    // Structured view produced by the exact Microsoft decoder port.  Its
    // ByteViews have the same lifetime as this ProgramView's source Bank.
    XenosShaderBinary::DecodedProgram decoded_program{};
    // VS113's stored vfetch packets are declaration placeholders.  These are
    // the exact packet words after the runtime patcher applies the cloud
    // FLOAT3/FLOAT2 declaration and DrawPrimitiveUP's 20-byte stride.
    bool runtime_vertex_fetches_valid = false;
    std::array<std::array<std::uint32_t, 3>, 2>
        runtime_vertex_fetch_words{};
    // Pixel compiler upload record start=0x1FC,count=0x10 sources 64 bytes
    // from ucode+0. Driver address arithmetic maps them to c252..c255;
    // c252/c253 are zero and the retained words below are c254/c255.
    bool embedded_pixel_constants_valid = false;
    std::array<std::array<std::uint32_t, 4>, 2>
        embedded_pixel_constant_words{};
};

struct RetailCloudShaders {
    bool valid = false;
    ProgramView vertex_shader{};
    // Entry order is 136, 137, 138.
    std::array<ProgramView, 3> pixel_shaders{};
};

struct RetailBackgroundMapShaders {
    bool valid = false;
    ProgramView vertex_shader{};
    ProgramView pixel_shader{};
};

// Accepts only the byte-exact retail cloud programs from the release bank.
// Besides the bank and table metadata, this verifies every binding descriptor
// and SHA-256 digests of each record, compiled shader, and Xenos ucode block.
bool ResolveRetailCloudShaders(const ShaderBank::Bank& bank,
                               RetailCloudShaders& out,
                               std::string* error = nullptr);

// Validates the byte-exact retail shaders used to generate the cloud
// background texture, including SBK metadata, hashes, bindings, and decoded
// Xenos packet anchors.
bool ResolveRetailBackgroundMapShaders(
    const ShaderBank::Bank& bank,
    RetailBackgroundMapShaders& out,
    std::string* error = nullptr);

// Exact branch at 0x8223A39C..0x8223A3C0.  Background-map enable/bind state
// does not affect selection: context 2 uses entry 137 whenever the actual
// caller argument is nonzero, otherwise 136; all other contexts use 138.
std::uint32_t SelectPixelShaderEntry(
    std::uint32_t render_context,
    std::uint32_t background_map_argument) noexcept;

// Returns the selected validated view, or nullptr for an unresolved set.
const ProgramView* SelectPixelShader(
    const RetailCloudShaders& shaders,
    std::uint32_t render_context,
    std::uint32_t background_map_argument) noexcept;

}  // namespace CloudShaderBank
