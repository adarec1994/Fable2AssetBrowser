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
