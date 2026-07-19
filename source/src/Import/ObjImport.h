#pragma once
#include <string>
#include <vector>

#include "GlbImport.h"

namespace ObjImport {

bool load_obj(const std::string& path, GlbImport::Scene& out,
              std::string& err);

std::vector<std::string> referenced_texture_files(const std::string& path);

}
