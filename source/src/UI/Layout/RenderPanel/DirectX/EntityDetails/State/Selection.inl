static int g_sel_spawn_marker = -1;
static int g_sel_pending_sp = -1;
static int g_sel_pending_gen = -1;
static bool g_marker_clear_selection = false;
static bool g_add_menu_requested = false;
static float g_add_menu_requested_pos[3] = {0, 0, 0};
static size_t g_player_start_placement = std::numeric_limits<size_t>::max();
