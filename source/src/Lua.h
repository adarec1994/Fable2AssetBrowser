#pragma once
#include <string>
#include <vector>
#include <cstdint>

// LuaFileUI is defined in State.h

std::vector<std::string> scan_luas_recursive(const std::string& root);
std::string read_lua_file_content(const std::string& path);
void dump_single_lua_file(const std::string& path, const std::string& root_dir);
void dump_all_lua_files(const std::string& root_dir);

// Lua 5.1 bytecode decompiler - returns decompiled source or error message
std::string decompile_lua51_bytecode(const uint8_t* data, size_t size);