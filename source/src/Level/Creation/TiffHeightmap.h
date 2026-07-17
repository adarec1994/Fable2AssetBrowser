#pragma once

#include <string>
#include <vector>






namespace Level {
namespace Creation {

bool LoadTiffHeightmap(const std::string& path, std::vector<float>& values,
                       int& width, int& height, std::string& error);

}
}
