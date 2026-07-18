void draw_model_in_panel(ID3D11Device* device) {
#include "ModelView/Scene/Setup.inl"

#ifdef _WIN32
#include "ModelView/Placement/DragDropAndStarts.inl"
#include "ModelView/Placement/AddMenu.inl"
#endif

#include "ModelView/Interaction/SkeletonCameraAndRender.inl"

#ifdef _WIN32
#include "ModelView/Editing/Markers.inl"
#endif

#include "ModelView/Editing/Selection/Setup.inl"
#include "ModelView/Editing/Selection/Metadata.inl"
#include "ModelView/Editing/Selection/TransformOverlay.inl"
#include "ModelView/Editing/Selection/Inventory.inl"
#include "ModelView/Editing/Selection/GizmoAndDelete.inl"
#include "ModelView/Controls/ViewportOverlays.inl"
#include "ModelView/Materials/MaterialsAndLods.inl"
#include "ModelView/Editing/Terrain.inl"
#include "ModelView/Animation/Controls.inl"
#include "ModelView/Details/Item.inl"
#include "ModelView/Details/Entity.inl"
#include "ModelView/Popout/Texture.inl"
}
