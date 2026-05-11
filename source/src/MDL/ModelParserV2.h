#pragma once

/* Experimental MDL parser that mirrors the game's actual reader
   (sub_82AB80C8 → sub_82B88AD0/E80 → sub_82AB85B8 → sub_82AB51F0 →
    four bucket readers at strides 20/24/28/36).

   See .claude/MDL_FORMAT_SPEC.md for the full spec.

   Gated behind S.dev_mode. When disabled, the existing parser runs
   unchanged. When enabled, parse_mdl_info / parse_mdl_geometry route
   to these functions instead. */

#include "ModelParser.h"

bool parse_mdl_info_v2(const std::vector<unsigned char>& data,
                       MDLInfo& out,
                       const std::string& file_path);

bool parse_mdl_geometry_v2(const std::vector<unsigned char>& data,
                           const MDLInfo& info,
                           std::vector<MDLMeshGeom>& out);
