#pragma once

struct FlatAssetEntry;

namespace Level {

enum class ExportFormat {
    GLB,
    FBX,
};

void ExportAsync(const FlatAssetEntry& entry, ExportFormat format);
bool IsExportInProgress();

}
