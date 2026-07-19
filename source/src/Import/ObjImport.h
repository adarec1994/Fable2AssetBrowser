#pragma once
// Wavefront .obj/.mtl loader for the import pipeline. Produces the same
// Scene structure as GlbImport (identical Fable-space basis change, winding
// flip, and top-left UV origin), so the material editor and the .mdl/.tex
// build path are shared. Referenced texture files are loaded from disk into
// Scene::Image blobs.
#include <string>
#include <vector>

#include "GlbImport.h"

namespace ObjImport {

bool load_obj(const std::string& path, GlbImport::Scene& out,
              std::string& err);

// Absolute paths of every texture file the obj's mtl set references (used
// by folder import to avoid importing those images a second time).
std::vector<std::string> referenced_texture_files(const std::string& path);

}
