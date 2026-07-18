bool Save(std::string& msg) {
    DebugLog::Scope debug_scope("Save level edits");
#include "Save/Setup.inl"
#include "Save/CollectEdits.inl"
#include "Save/Patching/LevelAndGdb.inl"
#include "Save/Entities/Rewrite.inl"
#include "Save/Entities/Physics.inl"
#include "Save/Bake/Setup.inl"
#include "Save/Bake/LevelData.inl"
#include "Save/Bake/Assets.inl"
#include "Save/Bake/Streaming.inl"
#include "Save/Finalize/DeferredState.inl"
#include "Save/Rebuild/Level.inl"
#include "Save/Rebuild/Entities.inl"
#include "Save/Rebuild/Text.inl"
#include "Save/Finalize/Commit.inl"
}
