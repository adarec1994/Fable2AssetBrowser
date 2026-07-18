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
