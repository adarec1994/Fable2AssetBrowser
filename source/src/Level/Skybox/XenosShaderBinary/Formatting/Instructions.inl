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
