#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace XenosShaderBinary {

enum class ControlFlowOpcode : std::uint8_t {
    Nop = 0,
    Exec = 1,
    ExecEnd = 2,
    ConditionalExec = 3,
    ConditionalExecEnd = 4,
    PredicatedConditionalExec = 5,
    PredicatedConditionalExecEnd = 6,
    Loop = 7,
    LoopEnd = 8,
    ConditionalCall = 9,
    Return = 10,
    ConditionalJump = 11,
    Allocate = 12,
    PredicateCleanConditionalExec = 13,
    PredicateCleanConditionalExecEnd = 14,
    VertexFetchEnd = 15,
};

enum class PredicateMode : std::uint8_t {
    None,
    IfFalse,
    IfTrue,
};

enum class ControlFlowConditionKind : std::uint8_t {
    None,
    Unconditional,
    Predicate,
    BooleanConstant,
};

enum class InstructionKind : std::uint8_t {
    Alu,
    Fetch,
};

struct ByteView {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
};

struct ProgramDescriptor {
    std::uint32_t descriptor_offset = 0;
    std::uint32_t subprogram_offset = 0;
    std::uint32_t subprogram_size = 0;

    std::uint32_t first_packet = 0;
};

struct ControlFlowInstruction {
    std::size_t index = 0;
    std::array<std::uint8_t, 6> decoder_bytes{};
    std::uint8_t opcode_value = 0;
    bool opcode_known = false;
    ControlFlowOpcode opcode = ControlFlowOpcode::Nop;
    bool has_exec_clause = false;
    std::uint16_t clause_address = 0;
    std::uint8_t clause_count = 0;
    bool yield = false;
    std::uint16_t sequence = 0;
    std::uint8_t mini_fetch_mask = 0;

    bool predicate_clean = false;
    ControlFlowConditionKind condition_kind =
        ControlFlowConditionKind::None;
    bool condition_expected = true;
    std::uint8_t boolean_constant = 0;

    bool has_target = false;
    std::uint16_t target = 0;
    bool forward_only = false;
    bool address_mode = false;
    bool repeat = false;
    std::uint8_t loop_id = 0;
    bool predicated_break = false;

    std::uint8_t allocation_size = 0;
    std::uint8_t allocation_type = 0;
    bool allocation_do_not_serialize = false;
};

struct VertexFetchInstruction {
    std::uint8_t opcode = 0;
    std::uint8_t destination = 0;
    bool destination_relative = false;
    std::array<std::uint8_t, 4> destination_swizzle{};
    std::uint8_t source = 0;
    std::uint8_t source_component = 0;
    std::uint8_t fetch_constant = 0;
    std::uint8_t fetch_constant_selector = 0;
    std::uint8_t prefetch_count = 0;

    std::uint8_t stride_dwords = 0;
    std::uint32_t offset_dwords = 0;
    std::uint8_t data_format = 0;
    bool signed_data = false;
    bool integer_format = false;
    bool round_index = false;
    std::uint8_t exponent_adjust = 0;
    bool mini_fetch = false;
    bool predicated = false;
    bool predicate_expected = true;
};

struct TextureFetchInstruction {
    std::uint8_t opcode = 0;
    std::uint8_t destination = 0;
    bool destination_relative = false;
    std::array<std::uint8_t, 4> destination_swizzle{};
    std::uint8_t source = 0;
    std::array<std::uint8_t, 3> source_swizzle{};
    std::uint8_t fetch_constant = 0;
    bool unnormalized_coordinates = false;

    std::uint8_t mag_filter = 0;
    std::uint8_t min_filter = 0;
    std::uint8_t mip_filter = 0;
    std::uint8_t aniso_filter = 0;
    std::uint8_t arbitrary_filter = 0;
    std::uint8_t volume_mag_filter = 0;
    std::uint8_t volume_min_filter = 0;
    bool use_computed_lod = false;
    std::uint8_t register_lod = 0;
    bool predicated = false;
    bool predicate_expected = true;

    bool use_register_gradients = false;
    bool centroid = false;
    std::uint8_t lod_bias_bits = 0;
    std::uint8_t offset_x_bits = 0;
    std::uint8_t offset_y_bits = 0;
    std::uint8_t offset_z_bits = 0;
};

enum class AluRegisterFile : std::uint8_t {
    Constant,
    Temporary,
};

struct AluDestination {
    std::uint8_t index = 0;
    bool export_register = false;
    bool relative = false;
    std::uint8_t write_mask = 0;
    std::uint8_t export_constant_mask = 0;
    bool saturate = false;
};

struct AluSource {
    bool valid = false;
    AluRegisterFile file = AluRegisterFile::Constant;
    std::uint8_t encoded = 0;
    std::uint8_t index = 0;
    bool relative = false;
    bool address_a0 = false;
    bool absolute = false;
    bool negate = false;
    bool literal_constant = false;

    std::uint8_t encoded_swizzle = 0;
    std::array<std::uint8_t, 4> swizzle{};
    std::uint8_t displayed_components = 0;
};

struct AluInstruction {

    std::uint8_t vector_opcode = 0;
    std::uint8_t vector_operand_count = 0;
    std::uint8_t scalar_opcode = 0;
    std::uint8_t scalar_operand_count = 0;
    std::uint8_t scalar_display_component_count = 0;
    bool vector_suppressed = false;
    bool scalar_suppressed = false;
    bool scalar_retain_previous = false;
    PredicateMode predicate = PredicateMode::None;
    AluDestination vector_destination{};
    AluDestination scalar_destination{};
    std::array<AluSource, 3> vector_sources{};
    std::array<AluSource, 2> scalar_sources{};

    std::string vector_disassembly;
    std::string scalar_disassembly;
};

struct ClauseInstruction {
    std::size_t control_flow_index = 0;
    std::uint16_t packet_index = 0;
    InstructionKind kind = InstructionKind::Alu;
    std::uint8_t sequence_bits = 0;
    bool serialize = false;
    bool mini_fetch = false;
    std::array<std::uint32_t, 3> words{};
    VertexFetchInstruction vertex_fetch{};
    TextureFetchInstruction fetch{};
    AluInstruction alu{};
};

struct VertexDeclarationElement {
    std::uint16_t stream = 0;
    std::uint16_t byte_offset = 0;
    std::uint32_t format = 0;
    std::uint8_t method = 0;
    std::uint8_t usage = 0;
    std::uint8_t usage_index = 0;
};

struct DecodedProgram {
    ProgramDescriptor descriptor{};
    ByteView full_ucode{};
    ByteView literal_prefix{};
    ByteView subprogram{};
    std::vector<ControlFlowInstruction> control_flow;
    std::vector<ClauseInstruction> clause_instructions;
};

bool DecodeProgram(ByteView compiled_shader,
                   ByteView xenos_ucode,
                   DecodedProgram& out,
                   std::string* error = nullptr);

TextureFetchInstruction DecodeTextureFetch(
    const std::array<std::uint32_t, 3>& words) noexcept;

VertexFetchInstruction DecodeVertexFetch(
    const std::array<std::uint32_t, 3>& words) noexcept;

AluInstruction DecodeAluInstruction(
    const std::array<std::uint32_t, 3>& words) noexcept;

std::array<std::uint32_t, 3> PatchVertexFetch(
    const std::array<std::uint32_t, 3>& words,
    const VertexDeclarationElement& element,
    std::uint8_t stride_code,
    std::uint16_t previous_availability = 0) noexcept;

const char* ControlFlowOpcodeName(std::uint8_t opcode) noexcept;
const char* FetchOpcodeName(std::uint8_t opcode) noexcept;
const char* VectorOpcodeName(std::uint8_t opcode) noexcept;
const char* ScalarOpcodeName(std::uint8_t opcode) noexcept;

}
