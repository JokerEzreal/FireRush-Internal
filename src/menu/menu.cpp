#include "menu.h"
#include "../gui/gui.h"
#include "../gui/esp.h"
#include "../cheats/cheats.h"
#include "../unity/unity_types.h"
#include "../core/globals.h"
#include "../core/game_data.h"

#include <cstdio>
#include <cstdarg>
#include <string>
#include <fstream>
#include <ctime>
#include <cstdlib>

namespace menu {

// ---------------------------------------------------------------------------
// Tab identifiers
// ---------------------------------------------------------------------------
enum Tab { TAB_ESP = 0, TAB_CHEATS = 1, TAB_VISUAL = 2, TAB_DEBUG = 3 };

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------
static bool  s_visible     = true;
static Tab   s_current_tab = TAB_ESP;

// Window position - default top-left
static float s_win_x = 10.0f;
static float s_win_y = 10.0f;
static float s_win_w = 320.0f;
static float s_prev_height = 250.0f;  // measured from previous frame

// Drag state
static bool  s_dragging   = false;
static float s_drag_off_x = 0.0f;
static float s_drag_off_y = 0.0f;
static constexpr float TITLE_BAR_H = 25.0f;

// Cached player data (refreshed periodically in on_update)
static const game_data::GamePlayerData* s_local_cache = nullptr;
static int s_player_count_cache = 0;
static int s_update_counter = 0;

// Dump state
static std::string s_last_dump_path;
static int  s_dump_status = 0;  // 0=none, 1=success, 2=fail
static int  s_dump_fade   = 0;  // frames left to show status

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static const char* fmt(const char* format, ...) {
    static char buf[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    return buf;
}

static const char* team_str(game_data::PlayerTeam t) {
    switch (t) {
        case game_data::PlayerTeam::Red:  return "Red";
        case game_data::PlayerTeam::Blue: return "Blue";
        default:                          return "NoTeam";
    }
}

// ---------------------------------------------------------------------------
// Data dump to file
// ---------------------------------------------------------------------------
static void dump_data_to_file() {
    char timestamp[64];
    time_t now = time(nullptr);
    struct tm t_buf;
    localtime_s(&t_buf, &now);
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &t_buf);

    // Write to Desktop
    const char* userprofile = getenv("USERPROFILE");
    std::string filepath;
    if (userprofile) {
        filepath = std::string(userprofile) + "\\Desktop\\firerush_dump_" + timestamp + ".txt";
    } else {
        char tmp[MAX_PATH];
        GetTempPathA(MAX_PATH, tmp);
        filepath = std::string(tmp) + "firerush_dump_" + timestamp + ".txt";
    }

    std::ofstream f(filepath);
    if (!f.is_open()) {
        s_dump_status = 2;
        s_last_dump_path = filepath;
        s_dump_fade = 300;
        return;
    }

    f << "========================================\n";
    f << "FireRush Internal - Data Dump\n";
    f << "Timestamp: " << timestamp << "\n";
    f << "========================================\n\n";

    // --- Feature state ---
    const esp::ESPConfig& cfg = esp::config();
    f << "=== FEATURE STATE ===\n";
    f << "ESP Enabled: " << (cfg.enabled ? "YES" : "NO") << "\n";
    f << "  Box: " << (cfg.show_box ? "ON" : "OFF")
      << "  Name: " << (cfg.show_name ? "ON" : "OFF")
      << "  HP: " << (cfg.show_health ? "ON" : "OFF") << "\n";
    f << "  Dist: " << (cfg.show_distance ? "ON" : "OFF")
      << "  Snap: " << (cfg.show_snapline ? "ON" : "OFF")
      << "  Team: " << (cfg.show_team ? "ON" : "OFF") << "\n";
    f << "  Max Distance: " << cfg.max_distance << "m\n\n";

    // --- Local player ---
    f << "=== LOCAL PLAYER ===\n";
    const auto* local = game_data::get_local_player();
    if (local) {
        f << "Name: " << (local->name.empty() ? "<empty>" : local->name) << "\n";
        f << "Team: " << team_str(local->team) << "\n";
        f << "HP: " << local->blood << "/" << local->max_blood << "\n";
        f << "Dead: " << (local->is_dead ? "YES" : "NO") << "\n";
        f << "PlayerID: " << local->player_id << "\n";
        f << "ServerID: " << (int)local->server_id << "\n";
        f << "Position: (" << local->world_position.x << ", "
          << local->world_position.y << ", " << local->world_position.z << ")\n";
        f << "Head: (" << local->head_position.x << ", "
          << local->head_position.y << ", " << local->head_position.z << ")\n";
    } else {
        f << "Not in game\n";
    }
    f << "\n";

    // --- All player data from game_data ---
    f << game_data::dump_all_data();
    f << "\n";

    // --- Debug info ---
    f << "=== DEBUG INFO ===\n";
    f << game_data::get_debug_info() << "\n\n";

    f << "========================================\n";
    f << "End of dump\n";
    f << "========================================\n";

    f.close();
    s_dump_status = 1;
    s_last_dump_path = filepath;
    s_dump_fade = 300;  // show status for ~5 seconds
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void initialize() {
    s_visible = true;
    s_current_tab = TAB_ESP;
    s_dragging = false;
    s_update_counter = 0;
    s_local_cache = nullptr;
    s_player_count_cache = 0;
    s_dump_status = 0;
    s_last_dump_path.clear();
    s_dump_fade = 0;
}

void on_update() {
    // Toggle menu visibility with Insert key
    if (gui::get_key_down(gui::keycode::Insert)) {
        s_visible = !s_visible;
    }

    // Toggle ESP with F1 key
    if (gui::get_key_down(gui::keycode::F1)) {
        esp::config().enabled = !esp::config().enabled;
    }

    // Update game data each frame
    game_data::update();

    // Apply cheats each frame
    cheats::on_update();

    // Refresh cached copies for menu display every ~15 frames
    if (++s_update_counter >= 15) {
        s_update_counter = 0;
        s_local_cache = game_data::get_local_player();
        s_player_count_cache = static_cast<int>(game_data::get_players().size());
    }
}

void on_gui_menu() {
    // Always render ESP overlay (even when menu is hidden)
    if (esp::config().enabled) {
        const auto& players = game_data::get_players();
        const auto* local   = game_data::get_local_player();
        if (local && !players.empty()) {
            esp::render(players, *local);
        }
    }

    if (!s_visible) return;

    // Layout constants
    constexpr float PAD     = 10.0f;
    constexpr float LINE_H  = 20.0f;
    constexpr float GAP     = 2.0f;
    constexpr float BTN_H   = 24.0f;

    // Handle dragging
    unity::Vector2 mouse = gui::get_mouse_position_gui();

    if (gui::get_mouse_button_down(0)) {
        if (mouse.x >= s_win_x && mouse.x <= s_win_x + s_win_w &&
            mouse.y >= s_win_y && mouse.y <= s_win_y + TITLE_BAR_H) {
            s_dragging   = true;
            s_drag_off_x = mouse.x - s_win_x;
            s_drag_off_y = mouse.y - s_win_y;
        }
    }
    if (!gui::get_mouse_button(0)) s_dragging = false;
    if (s_dragging) {
        s_win_x = mouse.x - s_drag_off_x;
        s_win_y = mouse.y - s_drag_off_y;
    }

    // Draw window background using previous frame's measured height
    float win_h = s_prev_height;
    gui::box(unity::Rect(s_win_x, s_win_y, s_win_w, win_h), "FireRush Internal v1.0");

    float x = s_win_x + PAD;
    float w = s_win_w - PAD * 2.0f;
    float y = s_win_y + 30.0f;  // below title bar

    // ================================================================
    // Tab bar (4 tabs: ESP, Cheats, Visual, Debug)
    // ================================================================
    constexpr float TAB_GAP = 4.0f;
    float tab_w = (w - TAB_GAP * 3.0f) / 4.0f;

    if (gui::button({x, y, tab_w, BTN_H},
            s_current_tab == TAB_ESP ? "[ ESP ]" : "ESP"))
        s_current_tab = TAB_ESP;

    if (gui::button({x + (tab_w + TAB_GAP), y, tab_w, BTN_H},
            s_current_tab == TAB_CHEATS ? "[ Cheats ]" : "Cheats"))
        s_current_tab = TAB_CHEATS;

    if (gui::button({x + (tab_w + TAB_GAP) * 2.0f, y, tab_w, BTN_H},
            s_current_tab == TAB_VISUAL ? "[ Visual ]" : "Visual"))
        s_current_tab = TAB_VISUAL;

    if (gui::button({x + (tab_w + TAB_GAP) * 3.0f, y, tab_w, BTN_H},
            s_current_tab == TAB_DEBUG ? "[ Debug ]" : "Debug"))
        s_current_tab = TAB_DEBUG;

    y += BTN_H + 4.0f;

    // Get a reference to ESP config for toggles
    esp::ESPConfig& cfg = esp::config();

    // ================================================================
    // TAB: ESP
    // ================================================================
    if (s_current_tab == TAB_ESP) {
        // Master toggle
        cfg.enabled = gui::toggle({x, y, w, LINE_H}, cfg.enabled, "ESP Enabled (F1)");
        y += LINE_H + GAP;

        y += 4.0f;
        gui::label({x, y, w, LINE_H}, "--- Elements ---");
        y += LINE_H + GAP;

        cfg.show_box = gui::toggle({x, y, w, LINE_H}, cfg.show_box, "Player Box");
        y += LINE_H + GAP;

        cfg.show_name = gui::toggle({x, y, w, LINE_H}, cfg.show_name, "Player Name");
        y += LINE_H + GAP;

        cfg.show_health = gui::toggle({x, y, w, LINE_H}, cfg.show_health, "Health Bar");
        y += LINE_H + GAP;

        cfg.show_distance = gui::toggle({x, y, w, LINE_H}, cfg.show_distance, "Distance");
        y += LINE_H + GAP;

        cfg.show_snapline = gui::toggle({x, y, w, LINE_H}, cfg.show_snapline, "Snap Lines");
        y += LINE_H + GAP;

        y += 4.0f;
        gui::label({x, y, w, LINE_H}, "--- Filters ---");
        y += LINE_H + GAP;

        cfg.show_team = gui::toggle({x, y, w, LINE_H}, cfg.show_team, "Show Teammates");
        y += LINE_H + GAP;

        // Max distance slider
        gui::label({x, y, w, LINE_H}, fmt("Max Distance: %.0fm", cfg.max_distance));
        y += LINE_H + GAP;
        cfg.max_distance = gui::horizontal_slider({x, y, w, LINE_H}, cfg.max_distance, 50.0f, 1000.0f);
        y += LINE_H + GAP;

        // Player stats summary
        y += 4.0f;
        gui::label({x, y, w, LINE_H}, "--- Info ---");
        y += LINE_H + GAP;

        const auto& players = game_data::get_players();
        const auto* local   = game_data::get_local_player();
        int total = static_cast<int>(players.size());
        int alive = 0;
        int enemies = 0;
        for (const auto& p : players) {
            if (!p.is_dead) {
                alive++;
                if (!p.is_local && local &&
                    (p.team != local->team || local->team == game_data::PlayerTeam::NoTeam)) {
                    enemies++;
                }
            }
        }

        gui::label({x, y, w, LINE_H},
            fmt("Players: %d | Alive: %d | Enemies: %d", total, alive, enemies));
        y += LINE_H + GAP;

        // Tools section
        y += 4.0f;
        gui::label({x, y, w, LINE_H}, "--- Tools ---");
        y += LINE_H + GAP;

        // Dump data button
        if (gui::button({x, y, w, BTN_H}, "Dump Data to File")) {
            dump_data_to_file();
        }
        y += BTN_H + GAP;

        // Dump status feedback
        if (s_dump_fade > 0) {
            s_dump_fade--;
            if (s_dump_status == 1) {
                gui::label({x, y, w, LINE_H}, "Dumped OK!");
                y += LINE_H + GAP;
                std::string display_path = s_last_dump_path;
                if (display_path.size() > 42)
                    display_path = "..." + display_path.substr(display_path.size() - 39);
                gui::label({x, y, w, LINE_H}, display_path.c_str());
                y += LINE_H + GAP;
            } else if (s_dump_status == 2) {
                gui::label({x, y, w, LINE_H}, "Dump FAILED - check permissions");
                y += LINE_H + GAP;
            }
        }

        // Footer
        y += 6.0f;
        gui::label({x, y, w, LINE_H}, "[Insert] Toggle | [End] Unload | [F1] ESP");
        y += LINE_H + GAP;
    }

    // ================================================================
    // TAB: Cheats
    // ================================================================
    else if (s_current_tab == TAB_CHEATS) {
        cheats::CheatConfig& cheat = cheats::config();

        // =============================================================
        // Aimbot
        // =============================================================
        gui::label({x, y, w, LINE_H}, "--- Aimbot ---");
        y += LINE_H + GAP;

        cheat.aimbot = gui::toggle({x, y, w, LINE_H}, cheat.aimbot, "Aimbot (RMB to aim)");
        y += LINE_H + GAP;

        gui::label({x, y, w, LINE_H}, fmt("  FOV: %.0f", cheat.aimbot_fov));
        y += LINE_H + GAP;
        cheat.aimbot_fov = gui::horizontal_slider({x, y, w, LINE_H}, cheat.aimbot_fov, 5.0f, 90.0f);
        y += LINE_H + GAP;

        gui::label({x, y, w, LINE_H}, fmt("  Smooth: %.1f", cheat.aimbot_smooth));
        y += LINE_H + GAP;
        cheat.aimbot_smooth = gui::horizontal_slider({x, y, w, LINE_H}, cheat.aimbot_smooth, 1.0f, 15.0f);
        y += LINE_H + GAP;

        // =============================================================
        // Auto-Fire - call Fire() N times per frame (PVP+PVE)
        // =============================================================
        y += 4.0f;
        gui::label({x, y, w, LINE_H}, "--- Auto Fire (PVP+PVE) ---");
        y += LINE_H + GAP;

        cheat.pve_fire_multiply = gui::toggle({x, y, w, LINE_H},
            cheat.pve_fire_multiply, "Auto Fire (call Fire() x N)");
        y += LINE_H + GAP;

        gui::label({x, y, w, LINE_H},
            fmt("  Shots/Frame: %d", cheat.pve_fire_extra));
        y += LINE_H + GAP;
        cheat.pve_fire_extra = static_cast<int>(gui::horizontal_slider(
            {x, y, w, LINE_H}, static_cast<float>(cheat.pve_fire_extra), 1.0f, 30.0f));
        y += LINE_H + GAP;

        // =============================================================
        // PVE Quick Combo
        // =============================================================
        y += 4.0f;
        gui::label({x, y, w, LINE_H}, "--- PVE Quick ---");
        y += LINE_H + GAP;

        if (gui::button({x, y, w, BTN_H}, "PVE Combo ON (All Boost)")) {
            cheat.pve_fire_multiply  = true;
            cheat.pve_fire_extra     = 15;
            cheat.infinite_ammo      = true;
            cheat.no_recoil          = true;
            cheat.no_spread          = true;
            cheat.god_mode           = true;
            cheat.weapon_boost       = true;
            cheat.pve_damage_boost   = true;
            // Disable PVE-breaking features
            cheat.bullet_speed       = false;
            cheat.range_hack         = false;
        }
        y += BTN_H + GAP;

        if (gui::button({x, y, w, BTN_H}, "PVE Combo OFF")) {
            cheat.pve_fire_multiply  = false;
            cheat.infinite_ammo      = false;
            cheat.no_recoil          = false;
            cheat.no_spread          = false;
            cheat.god_mode           = false;
            cheat.weapon_boost       = false;
            cheat.pve_damage_boost   = false;
        }
        y += BTN_H + GAP;

        // =============================================================
        // Movement
        // =============================================================
        y += 4.0f;
        gui::label({x, y, w, LINE_H}, "--- Movement ---");
        y += LINE_H + GAP;

        cheat.speed_hack = gui::toggle({x, y, w, LINE_H}, cheat.speed_hack, "Speed Hack");
        y += LINE_H + GAP;

        gui::label({x, y, w, LINE_H}, fmt("  Speed: %.1fx", cheat.speed_multiplier));
        y += LINE_H + GAP;
        cheat.speed_multiplier = gui::horizontal_slider({x, y, w, LINE_H}, cheat.speed_multiplier, 1.0f, 10.0f);
        y += LINE_H + GAP;

        // =============================================================
        // Weapon - works in both PVP and PVE
        // =============================================================
        y += 4.0f;
        gui::label({x, y, w, LINE_H}, "--- Weapon (PVP+PVE) ---");
        y += LINE_H + GAP;

        cheat.rapid_fire = gui::toggle({x, y, w, LINE_H}, cheat.rapid_fire, "Rapid Fire");
        y += LINE_H + GAP;

        cheat.infinite_ammo = gui::toggle({x, y, w, LINE_H}, cheat.infinite_ammo, "Infinite Ammo");
        y += LINE_H + GAP;

        cheat.no_recoil = gui::toggle({x, y, w, LINE_H}, cheat.no_recoil, "No Recoil");
        y += LINE_H + GAP;

        cheat.no_spread = gui::toggle({x, y, w, LINE_H}, cheat.no_spread, "No Spread");
        y += LINE_H + GAP;

        cheat.instant_switch = gui::toggle({x, y, w, LINE_H}, cheat.instant_switch, "Instant Switch");
        y += LINE_H + GAP;

        cheat.instant_reload = gui::toggle({x, y, w, LINE_H}, cheat.instant_reload, "Instant Reload");
        y += LINE_H + GAP;

        cheat.multi_bullet = gui::toggle({x, y, w, LINE_H}, cheat.multi_bullet, "Multi-Bullet");
        y += LINE_H + GAP;

        gui::label({x, y, w, LINE_H}, fmt("  Bullets/Shot: %d", cheat.multi_bullet_count));
        y += LINE_H + GAP;
        cheat.multi_bullet_count = static_cast<int>(gui::horizontal_slider(
            {x, y, w, LINE_H}, static_cast<float>(cheat.multi_bullet_count), 2.0f, 30.0f));
        y += LINE_H + GAP;

        // =============================================================
        // Weapon Boost (max damage factors + penetration)
        // =============================================================
        y += 4.0f;
        gui::label({x, y, w, LINE_H}, "--- Weapon Boost ---");
        y += LINE_H + GAP;

        cheat.weapon_boost = gui::toggle({x, y, w, LINE_H}, cheat.weapon_boost,
            "Max Damage Factors (100% all ranges)");
        y += LINE_H + GAP;

        cheat.pve_damage_boost = gui::toggle({x, y, w, LINE_H}, cheat.pve_damage_boost,
            "Damage Multiplier (Ref/HH)");
        y += LINE_H + GAP;

        gui::label({x, y, w, LINE_H}, fmt("  Damage: %.0fx", cheat.pve_damage_mul));
        y += LINE_H + GAP;
        cheat.pve_damage_mul = gui::horizontal_slider({x, y, w, LINE_H}, cheat.pve_damage_mul, 1.0f, 100.0f);
        y += LINE_H + GAP;

        // Weapon ID swap
        gui::label({x, y, w, LINE_H},
            fmt("  Current Weapon ID: %d", cheats::get_current_weapon_id()));
        y += LINE_H + GAP;

        cheat.weapon_id_swap = gui::toggle({x, y, w, LINE_H}, cheat.weapon_id_swap,
            "Weapon ID Swap");
        y += LINE_H + GAP;

        if (cheat.weapon_id_swap) {
            gui::label({x, y, w, LINE_H},
                fmt("  Target ID: %d", cheat.weapon_target_id));
            y += LINE_H + GAP;
            cheat.weapon_target_id = static_cast<int>(gui::horizontal_slider(
                {x, y, w, LINE_H}, static_cast<float>(cheat.weapon_target_id), 1.0f, 9999.0f));
            y += LINE_H + GAP;
        }

        // =============================================================
        // PVP-only (breaks PVE damage!)
        // =============================================================
        y += 4.0f;
        gui::label({x, y, w, LINE_H}, "--- PVP Only (breaks PVE!) ---");
        y += LINE_H + GAP;

        cheat.bullet_speed = gui::toggle({x, y, w, LINE_H}, cheat.bullet_speed,
            "Bullet Speed [!PVE=0 dmg]");
        y += LINE_H + GAP;

        gui::label({x, y, w, LINE_H}, fmt("  Bullet Speed: %.1fx", cheat.bullet_speed_mul));
        y += LINE_H + GAP;
        cheat.bullet_speed_mul = gui::horizontal_slider({x, y, w, LINE_H}, cheat.bullet_speed_mul, 1.0f, 10.0f);
        y += LINE_H + GAP;

        cheat.range_hack = gui::toggle({x, y, w, LINE_H}, cheat.range_hack,
            "Range Hack [!PVP only]");
        y += LINE_H + GAP;

        gui::label({x, y, w, LINE_H}, fmt("  Range: %.1fx", cheat.range_multiplier));
        y += LINE_H + GAP;
        cheat.range_multiplier = gui::horizontal_slider({x, y, w, LINE_H}, cheat.range_multiplier, 1.0f, 20.0f);
        y += LINE_H + GAP;

        // =============================================================
        // Defense + Melee
        // =============================================================
        y += 4.0f;
        gui::label({x, y, w, LINE_H}, "--- Defense ---");
        y += LINE_H + GAP;

        cheat.god_mode = gui::toggle({x, y, w, LINE_H}, cheat.god_mode, "God Mode (Infinite HP)");
        y += LINE_H + GAP;

        cheat.fast_knife = gui::toggle({x, y, w, LINE_H}, cheat.fast_knife, "Fast Knife");
        y += LINE_H + GAP;

        // =============================================================
        // Channel Unlock
        // =============================================================
        y += 4.0f;
        gui::label({x, y, w, LINE_H}, "--- Channel ---");
        y += LINE_H + GAP;

        cheat.channel_unlock = gui::toggle({x, y, w, LINE_H}, cheat.channel_unlock,
            "Unlock Channels (Lv+Honor)");
        y += LINE_H + GAP;

        // =============================================================
        // Status
        // =============================================================
        y += 4.0f;
        gui::label({x, y, w, LINE_H}, "--- Status ---");
        y += LINE_H + GAP;

        const char* dbg = cheats::get_debug_info();
        if (dbg && dbg[0]) {
            gui::label({x, y, w, LINE_H}, dbg);
            y += LINE_H + GAP;
        }

        // Footer
        y += 6.0f;
        gui::label({x, y, w, LINE_H}, "[Insert] Toggle | [End] Unload | [F1] ESP");
        y += LINE_H + GAP;
    }

    // ================================================================
    // TAB: Visual
    // ================================================================
    else if (s_current_tab == TAB_VISUAL) {
        gui::label({x, y, w, LINE_H}, "--- Visual Settings ---");
        y += LINE_H + GAP;

        gui::label({x, y, w, LINE_H}, "Game: firerush.exe");
        y += LINE_H + GAP;

        gui::label({x, y, w, LINE_H},
            fmt("Screen: %dx%d", gui::screen_width(), gui::screen_height()));
        y += LINE_H + GAP;

        gui::label({x, y, w, LINE_H},
            fmt("ESP: %s", cfg.enabled ? "ON" : "OFF"));
        y += LINE_H + GAP;

        y += 4.0f;
        gui::label({x, y, w, LINE_H}, "--- Active Features ---");
        y += LINE_H + GAP;

        gui::label({x, y, w, LINE_H},
            fmt("Box: %s  Name: %s  HP: %s",
                cfg.show_box ? "ON" : "OFF",
                cfg.show_name ? "ON" : "OFF",
                cfg.show_health ? "ON" : "OFF"));
        y += LINE_H + GAP;

        gui::label({x, y, w, LINE_H},
            fmt("Dist: %s  Snap: %s  Team: %s",
                cfg.show_distance ? "ON" : "OFF",
                cfg.show_snapline ? "ON" : "OFF",
                cfg.show_team ? "ON" : "OFF"));
        y += LINE_H + GAP;

        gui::label({x, y, w, LINE_H},
            fmt("Max Distance: %.0fm", cfg.max_distance));
        y += LINE_H + GAP;

        // Footer
        y += 6.0f;
        gui::label({x, y, w, LINE_H}, "[Insert] Toggle | [End] Unload | [F1] ESP");
        y += LINE_H + GAP;
    }

    // ================================================================
    // TAB: Debug
    // ================================================================
    else if (s_current_tab == TAB_DEBUG) {
        // --- Local player info ---
        gui::label({x, y, w, LINE_H}, "--- Local Player ---");
        y += LINE_H + GAP;

        const auto* local = game_data::get_local_player();
        if (!local) {
            gui::label({x, y, w, LINE_H}, "Not in game");
            y += LINE_H + GAP;
        } else {
            gui::label({x, y, w, LINE_H},
                fmt("Name: %s  |  Team: %s",
                    local->name.empty() ? "?" : local->name.c_str(),
                    team_str(local->team)));
            y += LINE_H + GAP;

            gui::label({x, y, w, LINE_H},
                fmt("HP: %d/%d  |  Dead: %s  |  ID: %d",
                    local->blood, local->max_blood,
                    local->is_dead ? "Y" : "N", local->player_id));
            y += LINE_H + GAP;

            gui::label({x, y, w, LINE_H},
                fmt("Pos: (%.1f, %.1f, %.1f)",
                    local->world_position.x, local->world_position.y,
                    local->world_position.z));
            y += LINE_H + GAP;
        }

        // --- HP Diagnostic: all sources side by side ---
        y += 4.0f;
        gui::label({x, y, w, LINE_H}, "--- HP Sources (all read each frame) ---");
        y += LINE_H + GAP;

        const auto& d = game_data::get_local_hp_diagnostic();

        gui::label({x, y, w, LINE_H},
            fmt("[A] BPI:     %d/%d %s%s",
                d.src_a_hp, d.src_a_maxhp,
                d.src_a_valid ? "" : "(null)",
                d.used_source == 'A' ? " <USED>" : ""));
        y += LINE_H + GAP;

        gui::label({x, y, w, LINE_H},
            fmt("[B] BPI_S:   %d/%d %s%s",
                d.src_b_hp, d.src_b_maxhp,
                d.src_b_valid ? "" : "(null)",
                d.used_source == 'B' ? " <USED>" : ""));
        y += LINE_H + GAP;

        gui::label({x, y, w, LINE_H},
            fmt("[C] Helth:   %d/%d %s%s",
                d.src_c_hp, d.src_c_maxhp,
                d.src_c_valid ? "" : "(null)",
                d.used_source == 'C' ? " <USED>" : ""));
        y += LINE_H + GAP;

        gui::label({x, y, w, LINE_H},
            fmt("[D] blood:   %d/%d%s",
                d.src_d_hp, d.src_d_maxhp,
                d.used_source == 'D' ? " <USED>" : ""));
        y += LINE_H + GAP;

        gui::label({x, y, w, LINE_H},
            fmt("[E] slider:  %.4f %s",
                d.src_e_slider,
                d.src_e_valid ? "" : "(null)"));
        y += LINE_H + GAP;

        gui::label({x, y, w, LINE_H},
            fmt("[F] label:   \"%s\" %s",
                d.src_f_valid ? d.src_f_label : "",
                d.src_f_valid ? "" : "(null)"));
        y += LINE_H + GAP;

        // HP logging toggle
        y += 4.0f;
        gui::label({x, y, w, LINE_H}, "--- HP Logging ---");
        y += LINE_H + GAP;

        if (game_data::is_hp_logging()) {
            gui::label({x, y, w, LINE_H}, "Logging: ON (Desktop/firerush_hp_log.txt)");
            y += LINE_H + GAP;
            if (gui::button({x, y, w, BTN_H}, "Stop HP Logging")) {
                game_data::stop_hp_logging();
            }
            y += BTN_H + GAP;
        } else {
            gui::label({x, y, w, LINE_H}, "Logging: OFF");
            y += LINE_H + GAP;
            if (gui::button({x, y, w, BTN_H}, "Start HP Logging (1/sec to file)")) {
                game_data::start_hp_logging();
            }
            y += BTN_H + GAP;
        }

        // --- All players ---
        y += 4.0f;
        const auto& players = game_data::get_players();
        gui::label({x, y, w, LINE_H},
            fmt("--- All Players (%d) ---", static_cast<int>(players.size())));
        y += LINE_H + GAP;

        int displayed = 0;
        for (int i = 0; i < static_cast<int>(players.size()); i++) {
            const auto& p = players[i];
            if (p.is_local) continue;

            if (displayed >= 10) {
                gui::label({x, y, w, LINE_H},
                    fmt("... +%d more", static_cast<int>(players.size()) - i));
                y += LINE_H + GAP;
                break;
            }

            gui::label({x, y, w, LINE_H},
                fmt("#%d %s%s %s HP:%d/%d [%c]",
                    i + 1, p.name.c_str(),
                    p.is_dead ? "[D]" : "",
                    team_str(p.team), p.blood, p.max_blood,
                    p.hp_diag.used_source));
            y += LINE_H + GAP;

            displayed++;
        }

        // --- Debug diagnostics ---
        y += 4.0f;
        gui::label({x, y, w, LINE_H}, "--- Diagnostics ---");
        y += LINE_H + GAP;

        const auto& dbg = game_data::get_debug_info();
        if (!dbg.empty()) {
            size_t pos = 0;
            int lines = 0;
            while (pos < dbg.size() && lines < 4) {
                size_t nl = dbg.find('\n', pos);
                std::string line = (nl == std::string::npos) ?
                    dbg.substr(pos) : dbg.substr(pos, nl - pos);
                if (!line.empty()) {
                    gui::label({x, y, w, LINE_H}, line.c_str());
                    y += LINE_H + GAP;
                    lines++;
                }
                if (nl == std::string::npos) break;
                pos = nl + 1;
            }
        } else {
            gui::label({x, y, w, LINE_H}, "(no debug info)");
            y += LINE_H + GAP;
        }

        gui::label({x, y, w, LINE_H},
            fmt("Players: %d", static_cast<int>(players.size())));
        y += LINE_H + GAP;

        // Footer
        y += 4.0f;
        gui::label({x, y, w, LINE_H}, "[Insert] Toggle | [End] Unload | [F1] ESP");
        y += LINE_H + GAP;
    }

    // Measure content height for next frame's box
    s_prev_height = (y - s_win_y) + PAD;
}

bool is_visible() {
    return s_visible;
}

} // namespace menu
