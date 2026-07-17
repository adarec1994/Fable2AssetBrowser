#pragma once

#include <string>

namespace ImportDialog {

void OpenGlb();
void OpenImage();
void OpenFolder();
void OpenTextureReplacement(const std::string& bnk_path, int file_index,
                            const std::string& file_name);

void Draw();

bool Busy();

}
