#pragma once

#include "../unity/unity_types.h"
#include <vector>

// Forward declarations
namespace game_data { struct GamePlayerData; }

namespace esp {

struct ESPConfig {
    bool enabled       = false;
    bool show_box      = true;
    bool show_name     = true;
    bool show_health   = true;
    bool show_distance = true;
    bool show_snapline = false;
    bool show_team     = false;   // render teammates as well
    float max_distance = 500.0f;
};

// Cache Camera class/methods. Call once after unity::cache_initialize().
bool initialize();

// Main render entry point -- call from the OnGUI callback.
void render(const std::vector<game_data::GamePlayerData>& players,
            const game_data::GamePlayerData& local_player);

// Mutable access to the config so the menu can toggle features.
ESPConfig& config();

} // namespace esp
