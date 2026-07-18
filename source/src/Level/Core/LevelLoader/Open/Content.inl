    loader_progress_update(22, 100, "Reading level GDB + save...");
    g_level_gdb_placements.clear();
    g_level_entity_contents.clear();
    g_level_entity_gameplay.clear();
    g_level_property_details.clear();
    g_level_entity_text.clear();
    g_level_spawn_markers.clear();
    g_level_creature_catalog.clear();
    {
#include "Level/Loading/Stages/LoadEntities.inl"
    loader_progress_update(26, 100, "Scanning level effects...");
#include "Level/Loading/Stages/LoadEffects.inl"
    loader_progress_update(28, 100, "Matching prop models...");
#include "Level/Loading/Stages/LoadProps.inl"
    }

    
    
    
    if (Creation::IsCustomLooseLevel(entry)) {
        const size_t cleared = g_level_gdb_placements.size();
        const size_t cleared_fx = g_pending_level_fx.size();
        g_level_gdb_placements.clear();
        g_pending_level_fx.clear();
        g_level_entity_contents.clear();
        g_level_entity_gameplay.clear();
        g_level_property_details.clear();
        if (cleared || cleared_fx) {
            OutputLog::info(
                "custom level: cleared " + std::to_string(cleared) +
                " donor entity placement(s) and " +
                std::to_string(cleared_fx) + " fx spawn(s)");
        }
    }
