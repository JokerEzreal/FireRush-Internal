#pragma once

#include "../unity/unity_types.h"
#include <string>
#include <vector>

namespace game_data {

// PlayerTeamEnum values mirroring FPSCmdEnum.PlayerTeamEnum (byte-backed)
enum class PlayerTeam : uint8_t {
    NoTeam = 0,
    Red    = 1,
    Blue   = 2
};

// HP diagnostic: stores readings from ALL sources each frame
struct HPDiagnostic {
    // Source A: Player.battlePlayer -> BPI.Hp/MaxHP
    int  src_a_hp       = 0;
    int  src_a_maxhp    = 0;
    bool src_a_valid    = false;  // was battlePlayer non-null?

    // Source B: BPI static singleton
    int  src_b_hp       = 0;
    int  src_b_maxhp    = 0;
    bool src_b_valid    = false;

    // Source C: NGUI_Helth.currentHP/maxHP
    int  src_c_hp       = 0;
    int  src_c_maxhp    = 0;
    bool src_c_valid    = false;

    // Source D: Player.blood/maxblood (legacy)
    int  src_d_hp       = 0;
    int  src_d_maxhp    = 0;

    // Source E: NGUI hpSlider.mValue (float 0-1, real HP ratio from UI)
    float src_e_slider  = -1.0f;  // -1 = not available
    bool  src_e_valid   = false;

    // Source F: NGUI hpLabel.mText (displayed HP string from UI)
    char  src_f_label[32] = {};   // displayed text e.g. "75"
    bool  src_f_valid   = false;

    // Which source was actually used for blood/max_blood ('A','B','C','D','E','?')
    char used_source    = '?';
};

struct GamePlayerData {
    int         player_id   = 0;
    uint8_t     server_id   = 0;
    std::string name;
    unity::Vector3 world_position;
    unity::Vector3 head_position;
    int         blood       = 0;
    int         max_blood   = 0;
    bool        is_dead     = false;
    bool        is_local    = false;
    PlayerTeam  team        = PlayerTeam::NoTeam;
    int         raw_layer   = -1;  // debug: raw LayerEnum value from ShowPlayerName
    HPDiagnostic hp_diag;          // diagnostic: all HP source readings
};

// Initialize: cache MonoClass/MonoMethod/MonoClassField pointers.
// Must be called after unity::cache_initialize() on the Mono thread.
bool initialize();

// Read all current player data from GameManager.Instance.Players.
// Safe to call each frame from the OnGUI thread.
void update();

// Thread-safe access to cached player data.
const std::vector<GamePlayerData>& get_players();

// Returns pointer to local player data, or nullptr if not found.
const GamePlayerData* get_local_player();

// Step-by-step diagnostic string built during update().
// Example: "PL=OK plr=OK name=Foo hp=100/100 pos=(1.0,2.0,3.0)"
// On failure: "ERR: PlayerLocal.Instance=null"
const std::string& get_debug_info();

// Returns a formatted multi-line string with ALL player data (for file dump).
std::string dump_all_data();

// HP diagnostic: returns the latest diagnostic for the local player.
const HPDiagnostic& get_local_hp_diagnostic();

// Start/stop periodic HP logging to file (writes every ~60 frames).
// Log file: %USERPROFILE%\Desktop\firerush_hp_log.txt
void start_hp_logging();
void stop_hp_logging();
bool is_hp_logging();

// Release cached references.
void shutdown();

} // namespace game_data
