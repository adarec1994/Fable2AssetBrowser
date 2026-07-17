#pragma once

#include <string>

#ifdef _WIN32
struct ID3D11Device;
#endif

namespace ImportDialog {

void OpenGlb();
void OpenImage();
void OpenFolder();
void OpenTextureReplacement(const std::string& bnk_path, int file_index,
                            const std::string& file_name);

#ifdef _WIN32
void Draw(::ID3D11Device* device);
#else
void Draw();
#endif

bool Busy();

}
