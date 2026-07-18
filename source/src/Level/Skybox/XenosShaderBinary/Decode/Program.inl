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
