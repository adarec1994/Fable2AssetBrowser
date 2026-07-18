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
