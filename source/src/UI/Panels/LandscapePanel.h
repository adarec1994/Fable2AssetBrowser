#pragma once

struct FlatAssetEntry;



namespace LandscapePanel {

bool AppliesTo(const FlatAssetEntry& entry);





void DrawSidePanel(const FlatAssetEntry& entry,
                   void* d3d_device = nullptr);


bool  InSculptMode();      
bool  InPaintMode();       
int   SculptTool();        
float BrushSize();         
float ToolStrength();      
float BrushFalloff();      

}
