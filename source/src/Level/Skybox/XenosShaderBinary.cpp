#include "XenosShaderBinary.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace XenosShaderBinary {
namespace {

constexpr std::array<const char*, 16> kControlFlowNames = {{
    "nop", "exec", "exec_end", "cexec", "cexec_end", "cexecp",
    "cexecp_end", "loop", "loop_end", "ccall", "ret", "cjmp",
    "alloc", "cexec", "cexec_end", "vfetche",
}};

constexpr std::array<const char*, 32> kFetchNames = {{
    "vfetch", "tfetch", "fetch3DNoiseMap", "fetchShadowMap",
    "fetchMultiSample", nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    "getCompTexLOD", "getBorderColorFraction", "getGradient",
    "getWeights", nullptr, nullptr, nullptr, nullptr,
    "setTexLOD", "setGradientH", "setGradientV", "setFilter4Weights",
    nullptr, nullptr, nullptr, nullptr,
}};

constexpr std::array<const char*, 32> kVectorNames = {{
    "add", "mul", "max", "min", "seq", "sgt", "sge", "sne",
    "frc", "trunc", "floor", "mad", "cndeq", "cndge", "cndgt",
    "dp4", "dp3", "dp2add", "cube", "max4", "setp_eq_push",
    "setp_ne_push", "setp_gt_push", "setp_ge_push", "kill_eq",
    "kill_gt", "kill_ge", "kill_ne", "dst", "maxa", "opcode_30",
    "opcode_31",
}};

constexpr std::array<std::uint8_t, 32> kVectorOperandCounts = {{
    2, 2, 2, 2, 2, 2, 2, 2,
    1, 1, 1, 3, 3, 3, 3, 2,
    2, 3, 2, 1, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 0, 0,
}};

constexpr std::array<const char*, 64> kScalarNames = {{
    "adds", "adds_prev", "muls", "muls_prev", "muls_prev2", "maxs",
    "mins", "seqs", "sgts", "sges", "snes", "frcs", "truncs",
    "floors", "exp", "logc", "log", "rcpc", "rcpf", "rcp", "rsqc",
    "rsqf", "rsq", "maxas", "maxasf", "subs", "subs_prev", "setp_eq",
    "setp_ne", "setp_gt", "setp_ge", "setp_inv", "setp_pop", "setp_clr",
    "setp_rstr", "kills_eq", "kills_gt", "kills_ge", "kills_ne",
    "kills_one", "sqrt", "opcode_41", "mulsc", "mulsc", "addsc",
    "addsc", "subsc", "subsc", "sin", "cos", "retain_prev",
    "opcode_51", "opcode_52", "opcode_53", "opcode_54", "opcode_55",
    "opcode_56", "opcode_57", "opcode_58", "opcode_59", "opcode_60",
    "opcode_61", "opcode_62", "opcode_63",
}};

constexpr std::array<std::uint8_t, 64> kScalarOperandCounts = {{
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 0, 1, 1, 1, 1, 1, 1,
    1, 0, 2, 2, 2, 2, 2, 2,
    1, 1, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
}};

constexpr std::array<std::uint8_t, 64> kScalarDisplayComponentCounts = {{
    2, 1, 2, 1, 2, 2, 2, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 2,
    2, 2, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
}};

std::uint32_t ReadBigEndian(const std::uint8_t* bytes) noexcept
{
    return (std::uint32_t(bytes[0]) << 24) |
           (std::uint32_t(bytes[1]) << 16) |
           (std::uint32_t(bytes[2]) << 8) |
           std::uint32_t(bytes[3]);
}

std::uint32_t ReadLittleEndian(const std::uint8_t* bytes) noexcept
{
    return std::uint32_t(bytes[0]) |
           (std::uint32_t(bytes[1]) << 8) |
           (std::uint32_t(bytes[2]) << 16) |
           (std::uint32_t(bytes[3]) << 24);
}

bool Fail(std::string* error, const char* detail)
{
    if (error != nullptr) *error = detail;
    return false;
}

bool IsExecOpcode(std::uint8_t opcode) noexcept
{
    return (opcode >= static_cast<std::uint8_t>(ControlFlowOpcode::Exec) &&
            opcode <= static_cast<std::uint8_t>(
                ControlFlowOpcode::PredicatedConditionalExecEnd)) ||
           opcode == static_cast<std::uint8_t>(
               ControlFlowOpcode::PredicateCleanConditionalExec) ||
           opcode == static_cast<std::uint8_t>(
               ControlFlowOpcode::PredicateCleanConditionalExecEnd);
}

std::string FormatDestination(std::uint8_t encoded,
                              std::uint8_t write_mask,
                              bool is_export,
                              bool clamp,
                              bool relative,
                              bool raw_seven,
                              std::uint8_t auxiliary_mask)
{

    const bool absolute = ((encoded >> 7) & 1u) != 0;
    const std::uint8_t index = encoded & 0x3fu;
    const bool brackets = !is_export && relative;
    const bool minus = write_mask == 0 && index != 16;
    std::string text = " ";
    if (absolute) text += '|';
    if (minus) {
        text += '-';
    } else {
        text += is_export ? 'o' : 'r';
    }
    if (brackets) text += '[';
    if (!minus) {
        if (is_export && index == 62) text += "Pos";
        else if (is_export && index == 63) text += "Pts";
        else text += std::to_string(index);
    }
    if (!is_export && relative) text += "+a0";
    if (brackets) text += ']';

    static constexpr char kComponents[] = "xyzw";
    if (is_export) {
        text += '.';
        for (unsigned component = 0; component < 4; ++component) {
            const unsigned bit = 1u << component;
            if ((write_mask & bit) != 0) {
                text += (auxiliary_mask & bit) != 0
                    ? '1' : kComponents[component];
            } else {
                text += ((auxiliary_mask & bit) != 0 || !raw_seven)
                    ? '_' : '0';
            }
        }
    } else if (write_mask != 0x0f) {
        text += '.';
        for (unsigned component = 0; component < 4; ++component) {
            if ((write_mask & (1u << component)) != 0) {
                text += kComponents[component];
            }
        }
    }
    if (absolute) text += '|';
    if (clamp) text += " clamp";
    return text;
}

std::array<char, 4> DecodeSourceSwizzle(std::uint8_t swizzle) noexcept
{
    static constexpr char kComponents[] = "xyzw";
    return {{
        kComponents[swizzle & 3u],
        kComponents[((swizzle >> 2) + 1u) & 3u],
        kComponents[((swizzle >> 4) - 2u) & 3u],
        kComponents[((swizzle >> 6) - 1u) & 3u],
    }};
}

std::array<std::uint8_t, 4> DecodeSourceSwizzleIndices(
    std::uint8_t swizzle) noexcept
{
    return {{
        static_cast<std::uint8_t>(swizzle & 3u),
        static_cast<std::uint8_t>(((swizzle >> 2) + 1u) & 3u),
        static_cast<std::uint8_t>(((swizzle >> 4) + 2u) & 3u),
        static_cast<std::uint8_t>(((swizzle >> 6) + 3u) & 3u),
    }};
}

PredicateMode DecodePredicateMode(std::uint32_t w1) noexcept
{
    const std::uint32_t encoded = (w1 >> 27) & 3u;
    if (encoded == 2) return PredicateMode::IfFalse;
    if (encoded == 3) return PredicateMode::IfTrue;
    return PredicateMode::None;
}

const char* PredicatePrefix(PredicateMode mode) noexcept
{
    if (mode == PredicateMode::IfFalse) return "(!p0) ";
    if (mode == PredicateMode::IfTrue) return "(p0) ";
    return "";
}

unsigned ExportContributionFlags(std::uint32_t w0) noexcept
{

    if ((w0 & 0x00008000u) == 0) return 0;
    const unsigned vector_mask = (w0 >> 16) & 0x0fu;
    const unsigned scalar_mask = (w0 >> 20) & 0x0fu;
    unsigned result = 0;
    for (unsigned component = 0; component < 4; ++component) {
        const unsigned bit = 1u << component;
        const unsigned category =
            ((vector_mask & bit) != 0 ? 2u : 0u) +
            ((scalar_mask & bit) != 0 ? 1u : 0u);
        if (category == 1) result |= 2u;
        else if (category == 2) result |= 1u;
        else if (category == 3) result |= 8u;
        else if ((w0 & 0x00004000u) != 0) result |= 4u;
    }
    return result;
}

AluDestination DecodeAluDestination(std::uint8_t index,
                                    bool export_register,
                                    bool relative,
                                    std::uint8_t write_mask,
                                    std::uint8_t constant_mask,
                                    bool saturate) noexcept
{
    AluDestination result{};
    result.index = index;
    result.export_register = export_register;
    result.relative = relative;
    result.write_mask = write_mask;
    result.export_constant_mask = constant_mask;
    result.saturate = saturate;
    return result;
}

AluSource DecodeAluSource(bool is_register,
                          std::uint8_t encoded,
                          bool relative,
                          bool address_a0,
                          bool constant_absolute,
                          bool negate,
                          std::uint8_t swizzle,
                          std::uint8_t displayed_components,
                          bool literal_constant = false) noexcept
{
    AluSource result{};
    result.valid = true;
    result.file = is_register
        ? AluRegisterFile::Temporary : AluRegisterFile::Constant;
    result.encoded = encoded;
    result.index = literal_constant
        ? encoded & 0x3fu
        : encoded & (is_register ? 0x3fu : 0xffu);
    result.relative = literal_constant
        ? false
        : (is_register ? (encoded & 0x40u) != 0 : relative);
    result.address_a0 = literal_constant ? false : address_a0;
    result.absolute = literal_constant
        ? (encoded & 0x80u) != 0
        : (is_register ? (encoded & 0x80u) != 0 : constant_absolute);
    result.negate = negate;
    result.literal_constant = literal_constant;
    result.encoded_swizzle = swizzle;
    result.swizzle = DecodeSourceSwizzleIndices(swizzle);
    result.displayed_components = displayed_components;
    return result;
}

bool DecodeRetailScalarRelative(std::uint32_t w1,
                                std::uint32_t w2) noexcept
{

    unsigned first_constant_source = 0;
    if ((w2 & 0x80000000u) == 0) first_constant_source = 1;
    else if ((w2 & 0x40000000u) == 0) first_constant_source = 2;
    else if ((w2 & 0x20000000u) == 0) first_constant_source = 3;
    const unsigned selector_bit = first_constant_source == 3 ? 31u : 30u;
    return ((w1 >> selector_bit) & 1u) != 0;
}

std::string FormatSource(bool is_register,
                         std::uint8_t encoded,
                         bool relative,
                         bool address_a0,
                         bool absolute,
                         bool negate,
                         std::uint8_t swizzle,
                         std::uint8_t component_count)
{

    const bool use_absolute =
        (!is_register && absolute) ||
        (is_register && (encoded & 0x80u) != 0);
    const bool register_relative =
        is_register && ((encoded >> 6) & 1u) != 0;
    const bool constant_relative = !is_register && relative;
    const std::uint8_t index = encoded & (is_register ? 0x3fu : 0xffu);
    std::string text;
    if (negate) text += '-';
    if (use_absolute) text += '|';
    text += is_register ? 'r' : 'c';
    if (register_relative || constant_relative) text += '[';
    text += std::to_string(index);
    if (register_relative) text += "+a0";
    if (constant_relative) text += address_a0 ? "+a0" : "+aL";
    if (register_relative || constant_relative) text += ']';

    if (swizzle != 0 || component_count < 4) {
        const std::array<char, 4> components = DecodeSourceSwizzle(swizzle);
        text += '.';
        text += components[0];
        if (component_count > 1 &&
            (component_count < 4 || components[1] != components[0] ||
             components[2] != components[0] ||
             components[3] != components[0])) {
            text += components[1];
            if (component_count > 2 &&
                (component_count < 4 || components[2] != components[1] ||
                 components[3] != components[1])) {
                text += components[2];
                if (component_count > 3 &&
                    components[3] != components[2]) {
                    text += components[3];
                }
            }
        }
    }
    if (use_absolute) text += '|';
    return text;
}

std::string FormatLiteralConstant(std::uint8_t encoded,
                                  bool negate,
                                  std::uint8_t swizzle)
{

    const bool absolute = (encoded & 0x80u) != 0;
    std::string text;
    if (negate) text += '-';
    if (absolute) text += '|';
    text += "k[" + std::to_string(encoded & 0x3fu) + ']';
    if (swizzle != 0) {
        static constexpr char kXyzw[] = "xyzw";
        static constexpr char kYzwx[] = "yzwx";
        static constexpr char kZwxy[] = "zwxy";
        static constexpr char kWxyz[] = "wxyz";
        text += '.';
        text += kXyzw[swizzle & 3u];
        text += kYzwx[(swizzle >> 2) & 3u];
        text += kZwxy[(swizzle >> 4) & 3u];
        text += kWxyz[(swizzle >> 6) & 3u];
    }
    if (absolute) text += '|';
    return text;
}

std::string FormatVectorAlu(const std::array<std::uint32_t, 3>& words,
                            const std::uint8_t* packet,
                            const AluInstruction& decoded)
{
    if (decoded.vector_suppressed) return {};
    const char* opcode = VectorOpcodeName(decoded.vector_opcode);
    std::string text = opcode ? opcode : "unknown_vector";
    const std::uint32_t w0 = words[0];
    const std::uint32_t w1 = words[1];
    const std::uint32_t w2 = words[2];
    if ((w0 & 0x01000000u) != 0) text += "_sat";
    text += FormatDestination(
        static_cast<std::uint8_t>(w0 & 0x3fu),

        static_cast<std::uint8_t>((w0 >> 16) & 0x0fu),
        ((w0 >> 15) & 1u) != 0,
        false,
        ((w0 >> 6) & 1u) != 0,
        ((w0 >> 14) & 1u) != 0,
        static_cast<std::uint8_t>((w0 >> 20) & 0x0fu));

    const bool literal_mode =
        (w1 & 0xe0000000u) == 0x20000000u;
    unsigned first_constant_source = 0;
    unsigned constant_source_count = 0;
    auto append_source = [&](unsigned source) {
        const bool is_register =
            ((w2 >> (32u - source)) & 1u) != 0;
        if (!is_register && first_constant_source == 0) {
            first_constant_source = source;
        }
        if (!is_register) ++constant_source_count;

        const std::uint8_t encoded = packet[8 + source];
        const std::uint8_t swizzle = packet[4 + source];
        const bool negate = ((w1 >> (27u - source)) & 1u) != 0;
        text += source == 1 ? ", " : ", ";
        if (!is_register && literal_mode) {
            text += FormatLiteralConstant(encoded, negate, swizzle);
            return;
        }

        bool relative = false;
        if (source == 1) {
            relative = ((w1 >> 31) & 1u) != 0;
        } else if (source == 2) {
            relative = ((w1 >>
                (first_constant_source == 2 ? 31u : 30u)) & 1u) != 0;
        } else {
            relative = ((w1 >>
                (first_constant_source == 3 || constant_source_count != 2
                    ? 31u : 30u)) & 1u) != 0;
        }
        text += FormatSource(
            is_register, encoded, relative,
            ((w1 >> 29) & 1u) != 0,
            ((w0 >> 7) & 1u) != 0,
            negate, swizzle, 4);
    };
    for (unsigned source = 1;
         source <= decoded.vector_operand_count; ++source) {
        append_source(source);
    }
    return text;
}

std::string FormatScalarAlu(const std::array<std::uint32_t, 3>& words,
                            const std::uint8_t* packet,
                            const AluInstruction& decoded)
{
    if (decoded.scalar_suppressed) return {};
    const char* opcode = ScalarOpcodeName(decoded.scalar_opcode);
    std::string text = opcode ? opcode : "unknown_scalar";
    const std::uint32_t w0 = words[0];
    const std::uint32_t w1 = words[1];
    const std::uint32_t w2 = words[2];
    if ((w0 & 0x02000000u) != 0) text += "_sat";
    const bool is_export = ((w0 >> 15) & 1u) != 0;
    const std::uint8_t destination = static_cast<std::uint8_t>(
        is_export ? (w0 & 0x3fu) : ((w0 >> 8) & 0x3fu));
    const bool destination_relative = is_export
        ? ((w0 >> 6) & 1u) != 0
        : ((w0 >> 14) & 1u) != 0;
    text += FormatDestination(
        destination,
        static_cast<std::uint8_t>((w0 >> 20) & 0x0fu),
        is_export,
        false,
        destination_relative,
        ((w0 >> 14) & 1u) != 0,
        static_cast<std::uint8_t>((w0 >> 16) & 0x0fu));

    if (decoded.scalar_operand_count == 0) return text;
    if (decoded.scalar_operand_count == 2) {

        const std::uint8_t constant_component =
            static_cast<std::uint8_t>(((w1 >> 6) - 1u) & 3u);
        text += ", ";
        text += FormatSource(
            false, static_cast<std::uint8_t>(w2),
            ((w1 >> 30) & 1u) != 0,
            ((w1 >> 29) & 1u) != 0,
            ((w0 >> 7) & 1u) != 0,
            ((w1 >> 24) & 1u) != 0,
            constant_component, 1);

        std::uint8_t register_index = static_cast<std::uint8_t>(
            (w1 & 0x3cu) | ((w0 >> 26) & 1u) |
            (((w2 >> 29) & 1u) << 1));
        if ((w0 & 0x80u) != 0) register_index |= 0x80u;
        text += ", ";
        text += FormatSource(
            true, register_index, false, false, false,
            (w1 & 1u) != 0,
            static_cast<std::uint8_t>(w1 & 3u), 1);
        return text;
    }

    text += ", ";
    const bool literal_mode =
        (w1 & 0xe0000000u) == 0x20000000u;
    const bool is_register = ((w2 >> 29) & 1u) != 0;
    const std::uint8_t encoded = packet[11];
    const bool negate = ((w1 >> 24) & 1u) != 0;
    const std::uint8_t swizzle = packet[7];
    if (!is_register && literal_mode) {
        text += FormatLiteralConstant(encoded, negate, swizzle);
        return text;
    }

    const bool relative = DecodeRetailScalarRelative(w1, w2);
    text += FormatSource(
        is_register, encoded, relative,
        ((w1 >> 29) & 1u) != 0,
        ((w0 >> 7) & 1u) != 0,
        negate, swizzle, decoded.scalar_display_component_count);
    return text;
}

std::uint32_t ExtractControlFlowBits(const std::uint8_t* bytes,
                                     std::size_t size,
                                     unsigned bit_one_based,
                                     unsigned width,
                                     bool& ok) noexcept
{

    if (bit_one_based == 0 || width == 0 || width > 31) {
        ok = false;
        return 0;
    }
    const std::size_t byte = (bit_one_based - 1u) >> 3u;
    const unsigned shift = (bit_one_based - 1u) & 7u;
    if (byte + 4 > size || shift + width > 32) {
        ok = false;
        return 0;
    }
    const std::uint32_t mask = (std::uint32_t(1) << width) - 1u;
    return (ReadLittleEndian(bytes + byte) >> shift) & mask;
}

std::vector<std::uint8_t> UndoRetailControlFlowWordSwap(ByteView subprogram)
{

    std::vector<std::uint8_t> decoder_bytes(
        subprogram.data, subprogram.data + subprogram.size);
    for (std::size_t offset = 0; offset + 4 <= decoder_bytes.size();
         offset += 4) {
        std::reverse(decoder_bytes.begin() + offset,
                     decoder_bytes.begin() + offset + 4);
    }
    return decoder_bytes;
}

AluInstruction DecodeAlu(const std::array<std::uint32_t, 3>& words,
                         const std::uint8_t* packet) noexcept
{
    const std::uint32_t w0 = words[0];
    const std::uint32_t w1 = words[1];
    const std::uint32_t w2 = words[2];
    AluInstruction result{};
    result.vector_opcode = packet[8] & 0x1fu;
    result.vector_operand_count =
        kVectorOperandCounts[result.vector_opcode];
    result.scalar_opcode = static_cast<std::uint8_t>(
        (w0 >> 26) & 0x3fu);
    result.scalar_operand_count =
        kScalarOperandCounts[result.scalar_opcode];
    result.scalar_retain_previous = result.scalar_opcode == 50;
    result.scalar_display_component_count =
        kScalarDisplayComponentCounts[result.scalar_opcode];
    result.predicate = DecodePredicateMode(w1);

    const bool export_register = (w0 & 0x00008000u) != 0;
    result.vector_destination = DecodeAluDestination(
        static_cast<std::uint8_t>(w0 & 0x3fu), export_register,
        (w0 & 0x40u) != 0,
        static_cast<std::uint8_t>((w0 >> 16) & 0x0fu),
        static_cast<std::uint8_t>((w0 >> 20) & 0x0fu),
        (w0 & 0x01000000u) != 0);
    result.scalar_destination = DecodeAluDestination(
        static_cast<std::uint8_t>(export_register
            ? (w0 & 0x3fu) : ((w0 >> 8) & 0x3fu)),
        export_register,
        export_register ? (w0 & 0x40u) != 0 : (w0 & 0x4000u) != 0,
        static_cast<std::uint8_t>((w0 >> 20) & 0x0fu),
        static_cast<std::uint8_t>((w0 >> 16) & 0x0fu),
        (w0 & 0x02000000u) != 0);

    const bool literal_mode = (w1 & 0xe0000000u) == 0x20000000u;
    unsigned first_constant_source = 0;
    unsigned constant_source_count = 0;
    for (unsigned source = 0;
         source < result.vector_operand_count; ++source) {
        const bool is_register = ((w2 >> (31u - source)) & 1u) != 0;
        const unsigned one_based = source + 1;
        if (!is_register && first_constant_source == 0) {
            first_constant_source = one_based;
        }
        if (!is_register) ++constant_source_count;
        bool relative = false;
        if (one_based == 1) {
            relative = ((w1 >> 31) & 1u) != 0;
        } else if (one_based == 2) {
            relative = ((w1 >>
                (first_constant_source == 2 ? 31u : 30u)) & 1u) != 0;
        } else {
            relative = ((w1 >>
                (first_constant_source == 3 || constant_source_count != 2
                    ? 31u : 30u)) & 1u) != 0;
        }
        result.vector_sources[source] = DecodeAluSource(
            is_register, packet[9 + source], relative,
            ((w1 >> 29) & 1u) != 0,
            ((w0 >> 7) & 1u) != 0,
            ((w1 >> (26u - source)) & 1u) != 0,
            packet[5 + source], 4,
            !is_register && literal_mode);
    }

    if (result.scalar_operand_count == 1) {
        const bool is_register = ((w2 >> 29) & 1u) != 0;
        const bool relative = DecodeRetailScalarRelative(w1, w2);
        result.scalar_sources[0] = DecodeAluSource(
            is_register, packet[11], relative,
            ((w1 >> 29) & 1u) != 0,
            ((w0 >> 7) & 1u) != 0,
            ((w1 >> 24) & 1u) != 0,
            packet[7], result.scalar_display_component_count,
            !is_register && literal_mode);
    } else if (result.scalar_operand_count == 2) {
        const std::uint8_t constant_swizzle =
            static_cast<std::uint8_t>(w1);
        result.scalar_sources[0] = DecodeAluSource(
            false, static_cast<std::uint8_t>(w2),
            ((w1 >> 30) & 1u) != 0,
            ((w1 >> 29) & 1u) != 0,
            ((w0 >> 7) & 1u) != 0,
            ((w1 >> 24) & 1u) != 0,
            constant_swizzle, 1, false);
        std::uint8_t register_index = static_cast<std::uint8_t>(
            (w1 & 0x3cu) | ((w0 >> 26) & 1u) |
            (((w2 >> 29) & 1u) << 1));
        if ((w0 & 0x80u) != 0) register_index |= 0x80u;
        const std::uint8_t register_swizzle = static_cast<std::uint8_t>(
            ((w1 + 1u) << 6) & 0xc0u);
        result.scalar_sources[1] = DecodeAluSource(
            true, register_index, false, false, false,
            (w1 & 1u) != 0,
            register_swizzle, 1);
    }

    const unsigned export_flags = ExportContributionFlags(w0);
    if (result.vector_opcode != 2) {
        result.vector_suppressed = false;
    } else if (!export_register) {
        result.vector_suppressed = ((w0 >> 16) & 0x0fu) == 0;
    } else {
        result.vector_suppressed =
            (export_flags & 1u) == 0 &&
            ((export_flags & 2u) != 0 || export_flags == 0);
    }
    result.scalar_suppressed = result.scalar_opcode == 50 &&
        (!export_register
            ? ((w0 >> 20) & 0x0fu) == 0
            : (export_flags & 2u) == 0);

    result.vector_disassembly = FormatVectorAlu(words, packet, result);
    result.scalar_disassembly = FormatScalarAlu(words, packet, result);
    const char* predicate = PredicatePrefix(result.predicate);
    if (predicate[0] != '\0') {
        if (!result.vector_disassembly.empty()) {
            result.vector_disassembly =
                std::string(predicate) + result.vector_disassembly;
        }
        if (!result.scalar_disassembly.empty()) {
            result.scalar_disassembly =
                std::string(predicate) + result.scalar_disassembly;
        }
    }
    return result;
}

}

VertexFetchInstruction DecodeVertexFetch(
    const std::array<std::uint32_t, 3>& words) noexcept
{

    const std::uint32_t w0 = words[0];
    const std::uint32_t w1 = words[1];
    const std::uint32_t w2 = words[2];
    VertexFetchInstruction result{};
    result.opcode = static_cast<std::uint8_t>(w0 & 0x1fu);
    result.destination = static_cast<std::uint8_t>((w0 >> 12) & 0x3fu);
    result.destination_relative = (w0 & 0x00040000u) != 0;
    result.destination_swizzle = {{
        static_cast<std::uint8_t>(w1 & 7u),
        static_cast<std::uint8_t>((w1 >> 3) & 7u),
        static_cast<std::uint8_t>((w1 >> 6) & 7u),
        static_cast<std::uint8_t>((w1 >> 9) & 7u),
    }};
    result.source = static_cast<std::uint8_t>(
        ((w0 >> 4) & 0x80u) | ((w0 >> 5) & 0x3fu));
    result.source_component = static_cast<std::uint8_t>((w0 >> 30) & 3u);
    result.fetch_constant = static_cast<std::uint8_t>((w0 >> 20) & 0x1fu);
    result.fetch_constant_selector =
        static_cast<std::uint8_t>((w0 >> 25) & 3u);
    result.prefetch_count =
        static_cast<std::uint8_t>(((w0 >> 27) & 7u) + 1u);
    result.stride_dwords = static_cast<std::uint8_t>(w2 & 0xffu);
    result.offset_dwords = (w2 >> 8) & 0x7fffffu;
    result.data_format = static_cast<std::uint8_t>((w1 >> 16) & 0x3fu);
    result.signed_data = (w1 & 0x00001000u) != 0;
    result.integer_format = (w1 & 0x00002000u) != 0;
    result.round_index = (w1 & 0x00008000u) != 0;
    result.exponent_adjust = static_cast<std::uint8_t>((w1 >> 24) & 0x3fu);
    result.mini_fetch = (w1 & 0x40000000u) != 0;
    result.predicated = (w1 & 0x80000000u) != 0;
    result.predicate_expected = (w2 & 0x80000000u) != 0;
    return result;
}

std::array<std::uint32_t, 3> PatchVertexFetch(
    const std::array<std::uint32_t, 3>& words,
    const VertexDeclarationElement& element,
    std::uint8_t stride_code,
    std::uint16_t previous_availability) noexcept
{

    const std::uint32_t stream = element.stream;
    const std::uint32_t fetch_constant =
        ((2u - (stream % 3u)) << 5) | (31u - stream / 3u);
    std::array<std::uint32_t, 3> out = words;
    out[0] = (words[0] & 0xc00fffffu) |
        ((fetch_constant & 0x7fu) << 20);
    out[1] = (words[1] & 0xbfc0cfffu) |
        ((element.format & 0x3fu) << 16) |
        ((element.format & 0x300u) << 4);
    out[2] = (words[2] & 0x80000000u) |
        ((std::uint32_t(element.byte_offset) << 6) & 0x7fffff00u) |
        stride_code;

    std::uint16_t available = static_cast<std::uint16_t>(
        ((((((((element.format >> 10) & 0x38u) |
                ((element.format >> 16) & 7u)) << 3) |
              ((element.format >> 19) & 7u)) << 1) |
            (element.format & 0xfc00u)) << 3) |
          (previous_availability & 0x0fu));
    if ((element.format & 0xc0u) == 0x40u) {
        if ((available & 0xe000u) <= 0x6000u) available ^= 0x2000u;
        if ((available & 0x1c00u) <= 0x0c00u) available ^= 0x0400u;
        if ((available & 0x0380u) <= 0x0180u) available ^= 0x0080u;
        if ((available & 0x0070u) <= 0x0030u) available ^= 0x0010u;
    }
    available |= 0x000eu;

    static constexpr std::array<std::uint16_t, 8> kSelectorMasks = {{
        0xe000u, 0x1c00u, 0x0380u, 0x0070u,
        0x0008u, 0x000au, 0x0008u, 0x000eu,
    }};
    auto put = [&](std::uint32_t mask, std::uint32_t value) {
        out[1] = (out[1] & ~mask) | (value & mask);
    };
    std::uint32_t mask = kSelectorMasks[out[1] & 7u] & available;
    std::uint32_t folded = (mask >> 3) | mask;
    folded = (folded >> 3) | mask;
    folded = (folded >> 3) | mask;
    put(0x0007u, folded >> 1);

    mask = kSelectorMasks[(out[1] >> 3) & 7u] & available;
    folded = (mask >> 3) | mask;
    folded = (folded >> 3) | mask;
    folded = (folded >> 3) | mask;
    put(0x0038u, (folded >> 1) | (mask << 2));

    mask = kSelectorMasks[(out[1] >> 6) & 7u] & available;
    folded = (mask >> 3) | mask;
    folded = (folded >> 3) | mask;
    put(0x01c0u,
        (folded >> 1) | (((mask << 3) | mask) << 2));

    mask = kSelectorMasks[(out[1] >> 9) & 7u] & available;
    folded = (mask << 3) | mask;
    folded = (folded << 3) | mask;
    put(0x0e00u,
        (folded << 2) | (((mask >> 3) | mask) >> 1));
    return out;
}

AluInstruction DecodeAluInstruction(
    const std::array<std::uint32_t, 3>& words) noexcept
{
    std::array<std::uint8_t, 12> packet{};
    for (std::size_t word = 0; word < words.size(); ++word) {
        packet[word * 4] = static_cast<std::uint8_t>(words[word] >> 24);
        packet[word * 4 + 1] =
            static_cast<std::uint8_t>(words[word] >> 16);
        packet[word * 4 + 2] =
            static_cast<std::uint8_t>(words[word] >> 8);
        packet[word * 4 + 3] = static_cast<std::uint8_t>(words[word]);
    }
    return DecodeAlu(words, packet.data());
}

TextureFetchInstruction DecodeTextureFetch(
    const std::array<std::uint32_t, 3>& words) noexcept
{

    const std::uint32_t w0 = words[0];
    const std::uint32_t w1 = words[1];
    const std::uint32_t w2 = words[2];
    TextureFetchInstruction result{};
    result.opcode = static_cast<std::uint8_t>(w0 & 0x1fu);
    result.destination = static_cast<std::uint8_t>((w0 >> 12) & 0x3fu);
    result.destination_relative = (w0 & 0x00040000u) != 0;
    result.destination_swizzle = {{
        static_cast<std::uint8_t>(w1 & 7u),
        static_cast<std::uint8_t>((w1 >> 3) & 7u),
        static_cast<std::uint8_t>((w1 >> 6) & 7u),
        static_cast<std::uint8_t>((w1 >> 9) & 7u),
    }};
    result.source = static_cast<std::uint8_t>(
        ((w0 >> 4) & 0x80u) | ((w0 >> 5) & 0x3fu));
    result.source_swizzle = {{
        static_cast<std::uint8_t>((w0 >> 26) & 3u),
        static_cast<std::uint8_t>((w0 >> 28) & 3u),
        static_cast<std::uint8_t>((w0 >> 30) & 3u),
    }};
    result.fetch_constant = static_cast<std::uint8_t>((w0 >> 20) & 0x1fu);
    result.unnormalized_coordinates = (w0 & 0x02000000u) != 0;

    result.mag_filter = static_cast<std::uint8_t>((w1 >> 12) & 3u);
    result.min_filter = static_cast<std::uint8_t>((w1 >> 14) & 3u);
    result.mip_filter = static_cast<std::uint8_t>((w1 >> 16) & 3u);
    result.aniso_filter = static_cast<std::uint8_t>((w1 >> 18) & 7u);
    result.arbitrary_filter = static_cast<std::uint8_t>((w1 >> 21) & 7u);
    result.volume_mag_filter =
        static_cast<std::uint8_t>((w1 >> 24) & 3u);
    result.volume_min_filter =
        static_cast<std::uint8_t>((w1 >> 26) & 3u);
    result.use_computed_lod = ((w1 >> 28) & 1u) != 0;
    result.register_lod = static_cast<std::uint8_t>((w1 >> 29) & 3u);
    result.predicated = (w1 >> 31) != 0;
    result.predicate_expected = (w2 >> 31) != 0;

    result.use_register_gradients = (w2 & 1u) != 0;
    result.centroid = ((w2 >> 1) & 1u) != 0;
    result.lod_bias_bits = static_cast<std::uint8_t>((w2 >> 2) & 0x7fu);
    result.offset_x_bits = static_cast<std::uint8_t>((w2 >> 16) & 0x1fu);
    result.offset_y_bits = static_cast<std::uint8_t>((w2 >> 21) & 0x1fu);
    result.offset_z_bits = static_cast<std::uint8_t>((w2 >> 26) & 0x1fu);
    return result;
}

bool DecodeProgram(ByteView compiled_shader,
                   ByteView xenos_ucode,
                   DecodedProgram& out,
                   std::string* error)
{
    out = DecodedProgram{};
    if (error != nullptr) error->clear();
    if (compiled_shader.data == nullptr || compiled_shader.size < 0x1cu) {
        return Fail(error, "compiled shader is too small for metadata root");
    }
    if (xenos_ucode.data == nullptr || xenos_ucode.size == 0) {
        return Fail(error, "Xenos ucode is empty");
    }

    const std::uint32_t metadata_size =
        ReadBigEndian(compiled_shader.data + 4);
    const std::uint32_t declared_ucode_size =
        ReadBigEndian(compiled_shader.data + 8);
    if (metadata_size > compiled_shader.size ||
        declared_ucode_size != xenos_ucode.size) {
        return Fail(error, "compiled metadata or ucode size mismatch");
    }

    const std::uint32_t descriptor_offset =
        ReadBigEndian(compiled_shader.data + 0x18);
    if (descriptor_offset > metadata_size ||
        metadata_size - descriptor_offset < 8) {
        return Fail(error, "subprogram descriptor is outside metadata");
    }
    const std::uint32_t subprogram_offset =
        ReadBigEndian(compiled_shader.data + descriptor_offset);
    const std::uint32_t subprogram_size =
        ReadBigEndian(compiled_shader.data + descriptor_offset + 4);
    if (subprogram_offset > xenos_ucode.size ||
        subprogram_size > xenos_ucode.size - subprogram_offset) {
        return Fail(error, "subprogram is outside Xenos ucode");
    }

    out.descriptor.descriptor_offset = descriptor_offset;
    out.descriptor.subprogram_offset = subprogram_offset;
    out.descriptor.subprogram_size = subprogram_size;
    out.full_ucode = xenos_ucode;
    out.literal_prefix = {xenos_ucode.data, subprogram_offset};
    out.subprogram = {
        xenos_ucode.data + subprogram_offset, subprogram_size};

    const std::vector<std::uint8_t> decoder_bytes =
        UndoRetailControlFlowWordSwap(out.subprogram);
    std::uint32_t first_packet = std::numeric_limits<std::uint32_t>::max();
    const std::size_t max_cf = decoder_bytes.size() / 6;
    for (std::size_t index = 0; index < max_cf; ++index) {
        if (first_packet != std::numeric_limits<std::uint32_t>::max() &&
            index / 2 == first_packet) {
            break;
        }
        const std::uint8_t* cf = decoder_bytes.data() + index * 6;
        const std::size_t available = decoder_bytes.size() - index * 6;
        bool ok = true;
        const std::uint8_t opcode = static_cast<std::uint8_t>(
            ExtractControlFlowBits(cf, available, 45, 4, ok));
        if (!ok) return Fail(error, "truncated control-flow instruction");

        ControlFlowInstruction decoded{};
        decoded.index = index;
        std::memcpy(decoded.decoder_bytes.data(), cf,
                    decoded.decoder_bytes.size());
        decoded.opcode_value = opcode;
        decoded.opcode_known = opcode <= 15;
        if (decoded.opcode_known) {
            decoded.opcode = static_cast<ControlFlowOpcode>(opcode);
        }
        decoded.address_mode =
            ExtractControlFlowBits(cf, available, 44, 1, ok) != 0;
        decoded.has_exec_clause = IsExecOpcode(opcode);
        if (decoded.has_exec_clause) {
            decoded.clause_address = static_cast<std::uint16_t>(
                ExtractControlFlowBits(cf, available, 1, 12, ok));
            decoded.clause_count = static_cast<std::uint8_t>(
                ExtractControlFlowBits(cf, available, 13, 3, ok));
            decoded.yield =
                ExtractControlFlowBits(cf, available, 16, 1, ok) != 0;
            decoded.sequence = static_cast<std::uint16_t>(
                ExtractControlFlowBits(cf, available, 17, 12, ok));
            decoded.mini_fetch_mask = static_cast<std::uint8_t>(
                ExtractControlFlowBits(cf, available, 29, 6, ok));

            if (opcode == 1 || opcode == 2 || opcode == 5 || opcode == 6) {
                decoded.predicate_clean =
                    ExtractControlFlowBits(cf, available, 42, 1, ok) != 0;
            } else if (opcode == 13 || opcode == 14) {
                decoded.predicate_clean = true;
            }
            if (opcode == 3 || opcode == 4 ||
                opcode == 13 || opcode == 14) {
                decoded.condition_kind =
                    ControlFlowConditionKind::BooleanConstant;
                decoded.boolean_constant = static_cast<std::uint8_t>(
                    ExtractControlFlowBits(cf, available, 35, 8, ok));
                decoded.condition_expected =
                    ExtractControlFlowBits(cf, available, 43, 1, ok) != 0;
            } else if (opcode == 5 || opcode == 6) {
                decoded.condition_kind =
                    ControlFlowConditionKind::Predicate;
                decoded.condition_expected =
                    ExtractControlFlowBits(cf, available, 43, 1, ok) != 0;
            }
            if (!ok) {
                return Fail(error, "truncated exec control-flow fields");
            }
            if ((opcode == 1 || opcode == 2) &&
                first_packet == std::numeric_limits<std::uint32_t>::max()) {
                first_packet = decoded.clause_address;
                const std::uint64_t prefix_size =
                    std::uint64_t(first_packet) * 12u;
                if (prefix_size > out.subprogram.size) {
                    return Fail(error,
                                "first clause address is outside subprogram");
                }
                out.descriptor.first_packet = first_packet;
            }
        } else if (opcode == 7) {
            decoded.has_target = true;
            decoded.target = static_cast<std::uint16_t>(
                ExtractControlFlowBits(cf, available, 1, 13, ok));
            decoded.repeat =
                ExtractControlFlowBits(cf, available, 14, 1, ok) != 0;
            decoded.loop_id = static_cast<std::uint8_t>(
                ExtractControlFlowBits(cf, available, 17, 5, ok));
        } else if (opcode == 8) {
            decoded.has_target = true;
            decoded.target = static_cast<std::uint16_t>(
                ExtractControlFlowBits(cf, available, 1, 13, ok));
            decoded.loop_id = static_cast<std::uint8_t>(
                ExtractControlFlowBits(cf, available, 17, 5, ok));
            decoded.predicated_break =
                ExtractControlFlowBits(cf, available, 22, 1, ok) != 0;
            if (decoded.predicated_break) {
                decoded.condition_kind =
                    ControlFlowConditionKind::Predicate;
                decoded.condition_expected =
                    ExtractControlFlowBits(cf, available, 43, 1, ok) != 0;
            }
        } else if (opcode == 9 || opcode == 11) {
            decoded.has_target = true;
            decoded.target = static_cast<std::uint16_t>(
                ExtractControlFlowBits(cf, available, 1, 13, ok));
            const bool unconditional =
                ExtractControlFlowBits(cf, available, 14, 1, ok) != 0;
            const bool predicated =
                ExtractControlFlowBits(cf, available, 15, 1, ok) != 0;
            decoded.forward_only =
                ExtractControlFlowBits(cf, available, 34, 1, ok) != 0;
            decoded.condition_expected =
                ExtractControlFlowBits(cf, available, 43, 1, ok) != 0;
            if (predicated) {
                decoded.condition_kind =
                    ControlFlowConditionKind::Predicate;
            } else if (unconditional) {
                decoded.condition_kind =
                    ControlFlowConditionKind::Unconditional;
            } else {
                decoded.condition_kind =
                    ControlFlowConditionKind::BooleanConstant;
                decoded.boolean_constant = static_cast<std::uint8_t>(
                    ExtractControlFlowBits(cf, available, 35, 8, ok));
            }
        } else if (opcode == 12) {
            decoded.allocation_size = static_cast<std::uint8_t>(
                ExtractControlFlowBits(cf, available, 1, 3, ok));
            decoded.allocation_do_not_serialize =
                ExtractControlFlowBits(cf, available, 41, 1, ok) != 0;
            decoded.allocation_type = static_cast<std::uint8_t>(
                ExtractControlFlowBits(cf, available, 42, 2, ok));
            decoded.allocation_debug = decoded.address_mode;
        }
        if (!ok) {
            return Fail(error, "truncated opcode-specific control-flow fields");
        }
        out.control_flow.push_back(decoded);
    }
    if (first_packet == std::numeric_limits<std::uint32_t>::max()) {
        return Fail(error, "control flow has no exec/exec_end entry clause");
    }
    if (out.control_flow.size() != std::size_t(first_packet) * 2u) {
        return Fail(error, "control-flow prefix length differs from clause address");
    }

    for (const ControlFlowInstruction& cf : out.control_flow) {
        if (!cf.has_exec_clause) continue;
        std::uint16_t sequence = cf.sequence;
        std::uint8_t mini_mask = cf.mini_fetch_mask;
        for (std::uint8_t item = 0; item < cf.clause_count; ++item) {
            const std::uint32_t packet_index =
                std::uint32_t(cf.clause_address) + item;
            const std::uint64_t packet_offset =
                std::uint64_t(packet_index) * 12u;
            if (packet_offset + 12u > out.subprogram.size) {
                return Fail(error, "exec clause packet is outside subprogram");
            }
            const std::uint8_t* packet =
                out.subprogram.data + static_cast<std::size_t>(packet_offset);
            ClauseInstruction instruction{};
            instruction.control_flow_index = cf.index;
            instruction.packet_index = static_cast<std::uint16_t>(packet_index);
            instruction.sequence_bits = static_cast<std::uint8_t>(
                sequence & 3u);
            instruction.kind = (sequence & 1u) != 0
                ? InstructionKind::Fetch : InstructionKind::Alu;
            instruction.serialize = (sequence & 2u) != 0;
            instruction.mini_fetch = (mini_mask & 1u) != 0;
            instruction.words = {{
                ReadBigEndian(packet),
                ReadBigEndian(packet + 4),
                ReadBigEndian(packet + 8),
            }};
            if (instruction.kind == InstructionKind::Fetch) {
                if ((instruction.words[0] & 0x1fu) == 0) {
                    instruction.vertex_fetch =
                        DecodeVertexFetch(instruction.words);
                } else {
                    instruction.fetch =
                        DecodeTextureFetch(instruction.words);
                }
            } else {
                instruction.alu = DecodeAlu(instruction.words, packet);
            }
            out.clause_instructions.push_back(instruction);
            sequence >>= 2;
            mini_mask >>= 1;
        }
    }
    return true;
}

const char* ControlFlowOpcodeName(std::uint8_t opcode) noexcept
{
    return opcode < kControlFlowNames.size()
        ? kControlFlowNames[opcode] : nullptr;
}

const char* FetchOpcodeName(std::uint8_t opcode) noexcept
{
    return opcode < kFetchNames.size() ? kFetchNames[opcode] : nullptr;
}

const char* VectorOpcodeName(std::uint8_t opcode) noexcept
{
    return opcode < kVectorNames.size() ? kVectorNames[opcode] : nullptr;
}

const char* ScalarOpcodeName(std::uint8_t opcode) noexcept
{
    return opcode < kScalarNames.size() ? kScalarNames[opcode] : nullptr;
}

}
