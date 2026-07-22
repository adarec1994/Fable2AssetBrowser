#pragma once

#include <string>
#include <vector>

struct FlatAssetEntry;



namespace LandscapePanel {

bool AppliesTo(const FlatAssetEntry& entry);




void DrawSidePanel(const FlatAssetEntry& entry,
                   void* d3d_device = nullptr);


bool  InSculptMode();
bool  InPaintMode();
bool  InFoliageMode();
int   SculptTool();
int   PaintTool();
float PaintNoiseScale();
float PaintNoiseCoverage();
float BrushSize();
float ToolStrength();
float BrushFalloff();

struct FoliagePaintEntry {
    std::string model_path;
    float density = 1.2f;
    float scale_min = 0.85f;
    float scale_max = 1.25f;
};

int   FoliageTool();
float FoliageBrushRadius();
bool  FoliageEraseMode();
void  FoliageEnabledPaintSet(std::vector<FoliagePaintEntry>& out);
const FoliagePaintEntry* FoliageActiveEntry();

}
