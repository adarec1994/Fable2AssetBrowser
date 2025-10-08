#pragma once
#include <string>
#include <vector>
#include <filesystem>

std::string load_last_dir();
void save_last_dir(const std::string &p);
std::vector<std::string> scan_bnks_recursive(const std::string &root);
std::vector<unsigned char> read_all_bytes(const std::filesystem::path &p);
bool rd32be(const std::vector<unsigned char> &d, size_t o, uint32_t &v);
bool rd16be(const std::vector<unsigned char> &d, size_t o, uint16_t &v);