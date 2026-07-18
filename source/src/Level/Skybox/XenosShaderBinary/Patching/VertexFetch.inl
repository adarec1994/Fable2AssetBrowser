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
