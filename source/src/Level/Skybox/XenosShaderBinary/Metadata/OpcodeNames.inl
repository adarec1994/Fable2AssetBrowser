const char* ControlFlowOpcodeName(std::uint8_t opcode) noexcept
{
    return opcode < kControlFlowNames.size()
        ? kControlFlowNames[opcode] : nullptr;
}

const char* FetchOpcodeName(std::uint8_t opcode) noexcept
{
    return opcode < kFetchNames.size() ? kFetchNames[opcode] : nullptr;
}

const char* VectorOpcodeName(std::uint8_t opcode) noexcept
{
    return opcode < kVectorNames.size() ? kVectorNames[opcode] : nullptr;
}

const char* ScalarOpcodeName(std::uint8_t opcode) noexcept
{
    return opcode < kScalarNames.size() ? kScalarNames[opcode] : nullptr;
}
