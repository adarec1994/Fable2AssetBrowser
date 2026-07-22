#pragma once

#include <string>

namespace VfsConfigViewer {

bool IsFileName(const std::string& name);

bool OpenBnkEntry(const std::string& bnk_path, int file_index,
                  const std::string& virtual_path);

void Draw(const std::string* content);

}
