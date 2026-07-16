#pragma once

#include "Level/Rendering/ShaderBankFile.h"
#include "XenosShaderBinary.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace CloudShaderBank {

constexpr std::uint32_t kVertexShaderEntry = 135;
constexpr std::uint32_t kPixelShaderEntryContext2 = 136;
constexpr std::uint32_t kPixelShaderEntryContext2Background = 137;
constexpr std::uint32_t kPixelShaderEntryOtherContext = 138;

constexpr std::uint32_t kBackgroundMapVertexShaderEntry = 85;
constexpr std::uint32_t kBackgroundMapPixelShaderEntry = 86;

struct ByteView {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
};

struct ProgramView {
    std::uint32_t shader_entry = 0;
    std::uint32_t program_ordinal = 0;
    const ShaderBank::ShaderEntry* shader = nullptr;
    const ShaderBank::Program* program = nullptr;
    ByteView record{};
    ByteView compiled_shader{};
    ByteView xenos_ucode{};

    XenosShaderBinary::DecodedProgram decoded_program{};

    bool runtime_vertex_fetches_valid = false;
    std::array<std::array<std::uint32_t, 3>, 2>
        runtime_vertex_fetch_words{};

    bool embedded_pixel_constants_valid = false;
    std::array<std::array<std::uint32_t, 4>, 2>
        embedded_pixel_constant_words{};
};

struct RetailCloudShaders {
    bool valid = false;
    ProgramView vertex_shader{};

    std::array<ProgramView, 3> pixel_shaders{};
};

struct RetailBackgroundMapShaders {
    bool valid = false;
    ProgramView vertex_shader{};
    ProgramView pixel_shader{};
};

bool ResolveRetailCloudShaders(const ShaderBank::Bank& bank,
                               RetailCloudShaders& out,
                               std::string* error = nullptr);

bool ResolveRetailBackgroundMapShaders(
    const ShaderBank::Bank& bank,
    RetailBackgroundMapShaders& out,
    std::string* error = nullptr);

std::uint32_t SelectPixelShaderEntry(
    std::uint32_t render_context,
    std::uint32_t background_map_argument) noexcept;

const ProgramView* SelectPixelShader(
    const RetailCloudShaders& shaders,
    std::uint32_t render_context,
    std::uint32_t background_map_argument) noexcept;

}
