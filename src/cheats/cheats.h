#pragma once

namespace cheats {

struct CheatConfig {
    // Speed hack
    bool  speed_hack       = false;
    float speed_multiplier = 2.0f;    // 1.0 = normal

    // Bullet speed
    bool  bullet_speed     = false;
    float bullet_speed_mul = 3.0f;

    // Infinite ammo (magazine + reserve)
    bool  infinite_ammo    = false;

    // Rapid fire (zero gun cooldown)
    bool  rapid_fire       = false;

    // Fast knife (zero ALL melee timers)
    bool  fast_knife       = false;

    // No recoil
    bool  no_recoil        = false;

    // No spread
    bool  no_spread        = false;

    // Instant weapon switch
    bool  instant_switch   = false;

    // Instant reload
    bool  instant_reload   = false;

    // Multi-bullet per shot (shotgun mode)
    bool  multi_bullet       = false;
    int   multi_bullet_count = 5;     // bullets per shot

    // Range hack (extend attack/melee range)
    bool  range_hack       = false;
    float range_multiplier = 5.0f;

    // God mode (keep blood == maxblood each frame)
    bool  god_mode         = false;

    // PVE damage boost (multiply weapon base damage Reference/HightHurt)
    bool  pve_damage_boost   = false;
    float pve_damage_mul     = 10.0f;

    // PVE fire multiply (resend CS_FirePve packet N extra times per shot)
    bool  pve_fire_multiply   = false;
    int   pve_fire_extra      = 5;      // extra copies of each fire packet

    // Weapon boost (maximize distance damage factors + enable penetration)
    // Effective for PVP (client-side damage calc). PVE damage is server-side.
    bool  weapon_boost        = false;

    // Weapon ID swap (change WAC.ID to target weapon ID)
    bool  weapon_id_swap      = false;
    int   weapon_target_id    = 0;       // 0 = disabled

    // Aimbot (hold RMB to aim at nearest enemy head)
    bool  aimbot             = false;
    float aimbot_fov         = 30.0f;    // FOV cone degrees
    float aimbot_smooth      = 3.0f;     // 1 = instant snap, higher = smoother

    // Channel unlock (bypass level + honor restrictions)
    bool  channel_unlock     = false;
};

// One-time setup. Call after game_data::initialize().
bool initialize();

// Called every frame during the Update callback.
void on_update();

// Access config for menu toggles.
CheatConfig& config();

// Debug string for diagnostics.
const char* get_debug_info();

// Current weapon ID (read each frame for diagnostics).
int get_current_weapon_id();

// PVE packet commands (called from menu buttons).
bool send_kill_all_mons();
bool send_damage_all_mons();

// Cleanup.
void shutdown();

} // namespace cheats
