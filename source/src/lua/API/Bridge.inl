std::string decompile_lua51_bytecode(const uint8_t* data, size_t size) {
    return lua::decompile_lua_bytecode(data, size);
}
