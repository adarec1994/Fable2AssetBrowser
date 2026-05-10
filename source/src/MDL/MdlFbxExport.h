// MdlFbxExport — convert a parsed MDL into a binary FBX 7400 file.
//
// The output FBX carries everything `mdl_to_glb_full` lands in its
// glTF: positions / normals / UVs, indexed geometry, per-mesh
// materials with PBR-ish defaults, skeleton hierarchy as LimbNode
// chains, skin clusters with per-vertex weights, and diffuse PNG
// textures **embedded** as Video.Content blobs (Blender / Maya pick
// these up automatically — no sidecar files).
//
// Writer is binary FBX 7400 (FBX 2014 — FBX 2020 readers all accept
// it). 7500+ uses 64-bit offsets which add complexity for no benefit
// here; we stay on 7400. Embedded textures use the standard `Video`
// node's `Content` raw-bytes property so importers don't need any
// extra hints to pull the PNG out.
//
// Failure modes mirror mdl_to_glb_full: returns false with `err_msg`
// set on parse error, geometry-empty, or write failure. Caller can
// surface that through OutputLog.

#pragma once

#include <string>
#include <vector>

bool mdl_to_fbx_full(const std::vector<unsigned char>& mdl_data,
                     const std::string& fbx_path,
                     const std::string& mdl_source_path,
                     std::string& err_msg);
