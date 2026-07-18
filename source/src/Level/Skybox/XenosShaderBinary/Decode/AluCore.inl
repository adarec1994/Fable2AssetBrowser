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
