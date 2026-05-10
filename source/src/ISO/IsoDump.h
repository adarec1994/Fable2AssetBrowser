#pragma once

#include "../textures/export/TextureExport.h"

namespace ISO {

void dump_iso_contents();

void dump_mdl_files();

enum class MdlExportFormat {
    GLB,
    FBX,
    RAW,
};

void mdl_export_begin_named(MdlExportFormat fmt,
                            const std::string& bnk_path,
                            int file_index,
                            const std::string& display_path,
                            bool from_nested);

void dump_mdl_files_as(MdlExportFormat fmt);

void dump_tex_files();

void dump_tex_files_as(TexExportFormat fmt);

void dump_wav_files();

enum class AudioExportFormat {
    WAV_RAW,
    WAV_PCM,
    MP3,
    AAC,
};

void dump_wav_files_as(AudioExportFormat fmt);

void dump_bnk_contents();

}
