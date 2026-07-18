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
