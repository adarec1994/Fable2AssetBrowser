#pragma once




namespace DetailsPanel {


bool Active();

bool WaterSelected();

bool SelectedWaterPosition(float out_engine_pos[3]);

void MoveSelectedWater(const float engine_step[3]);

bool WaterPreviewOffset(float out_render_offset[3]);

void ClearSelection();


void Draw();

}
