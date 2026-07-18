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
