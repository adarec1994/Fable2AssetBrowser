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
