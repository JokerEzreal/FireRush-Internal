#include "esp.h"
#include "gui.h"
#include "../core/game_data.h"
#include "../mono/mono_api.h"
#include "../mono/mono_types.h"
#include "../unity/unity_classes.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace esp {

// ---------------------------------------------------------------------------
// Config singleton
// ---------------------------------------------------------------------------
static ESPConfig s_config;

ESPConfig& config() { return s_config; }

// ---------------------------------------------------------------------------
// Cached Mono handles for Camera
// ---------------------------------------------------------------------------
static MonoClass*  s_camera_class          = nullptr;
static MonoMethod* s_camera_get_main       = nullptr;
static MonoMethod* s_camera_w2s            = nullptr;  // WorldToScreenPoint(Vector3)

// White 1x1 texture for drawing filled rectangles (GC-pinned via gchandle)
static MonoObject* s_white_texture = nullptr;
static uint32_t    s_white_texture_gchandle = 0;

// ---------------------------------------------------------------------------
// Exception-safe invoke (same pattern as gui.cpp)
// ---------------------------------------------------------------------------
static MonoObject* invoke_safe(MonoMethod* method, MonoObject* obj = nullptr,
                               void** params = nullptr) {
    if (!method) return nullptr;
    MonoObject* exc = nullptr;
    MonoObject* ret = mono::runtime_invoke(method, obj, params, &exc);
    return exc ? nullptr : ret;
}

// ---------------------------------------------------------------------------
// Create a 1x1 white Texture2D and pin it against GC + scene unload
// ---------------------------------------------------------------------------
static MonoObject* create_white_texture() {
    MonoClass*  tex_class  = unity::classes().Texture2D;
    MonoMethod* tex_ctor   = unity::methods().Texture2D_ctor_2;
    MonoMethod* set_pixel  = unity::methods().Texture2D_SetPixel;
    MonoMethod* apply      = unity::methods().Texture2D_Apply_0;

    if (!tex_class || !tex_ctor || !set_pixel || !apply)
        return nullptr;

    MonoDomain* domain = mono::domain_get();
    if (!domain) return nullptr;

    MonoObject* tex = mono::object_new(domain, tex_class);
    if (!tex) return nullptr;

    // .ctor(int width, int height)
    int w = 1, h = 1;
    void* ctor_args[2] = { &w, &h };
    invoke_safe(tex_ctor, tex, ctor_args);

    // SetPixel(0, 0, Color.white)
    int px = 0, py = 0;
    unity::Color white = unity::Color::white();
    void* sp_args[3] = { &px, &py, &white };
    invoke_safe(set_pixel, tex, sp_args);

    // Apply()
    invoke_safe(apply, tex);

    // Pin with GC handle to prevent garbage collection
    if (mono::gchandle_new) {
        s_white_texture_gchandle = mono::gchandle_new(tex, 0);
    }

    // DontDestroyOnLoad to survive scene transitions
    MonoMethod* ddol = unity::methods().Object_DontDestroyOnLoad;
    if (ddol) {
        void* ddol_args[1] = { tex };
        invoke_safe(ddol, nullptr, ddol_args);
    }

    // hideFlags = HideAndDontSave (61) to prevent unload
    MonoMethod* set_hf = unity::methods().Object_set_hideFlags;
    if (set_hf) {
        int flags = 61; // HideFlags.HideAndDontSave
        void* hf_args[1] = { &flags };
        invoke_safe(set_hf, tex, hf_args);
    }

    return tex;
}

// ---------------------------------------------------------------------------
// Ensure white texture is available (lazy create / recreate)
// ---------------------------------------------------------------------------
static MonoObject* ensure_white_texture() {
    if (s_white_texture) return s_white_texture;
    s_white_texture = create_white_texture();
    return s_white_texture;
}

// ---------------------------------------------------------------------------
// initialize
// ---------------------------------------------------------------------------
bool initialize() {
    MonoImage* engine_img = unity::images().unity_engine;
    if (!engine_img) return false;

    s_camera_class = mono::class_from_name(engine_img, "UnityEngine", "Camera");
    if (!s_camera_class) return false;

    s_camera_get_main = mono::class_get_method_from_name(s_camera_class, "get_main", 0);
    s_camera_w2s      = mono::class_get_method_from_name(s_camera_class, "WorldToScreenPoint", 1);

    if (!s_camera_get_main || !s_camera_w2s) return false;

    // Create white texture (with GC pin + DontDestroyOnLoad)
    s_white_texture = create_white_texture();
    if (!s_white_texture) return false;

    return true;
}

// ---------------------------------------------------------------------------
// WorldToScreenPoint wrapper
// Returns GUI coordinates (Y=0 at top). Sets 'visible' to false if behind camera.
// ---------------------------------------------------------------------------
static unity::Vector2 world_to_screen(const unity::Vector3& world_pos, bool& visible) {
    visible = false;

    // Camera.main
    MonoObject* camera = invoke_safe(s_camera_get_main);
    if (!camera) return {};

    // WorldToScreenPoint(Vector3) -> returns boxed Vector3
    unity::Vector3 pos = world_pos;
    void* args[1] = { &pos };
    MonoObject* result = invoke_safe(s_camera_w2s, camera, args);
    if (!result) return {};

    auto* screen = static_cast<unity::Vector3*>(mono::object_unbox(result));
    if (!screen) return {};

    // z < 0 means behind camera
    if (screen->z <= 0.0f) return {};

    visible = true;

    // Flip Y: Unity screen Y=0 at bottom, GUI Y=0 at top
    float sh = static_cast<float>(gui::screen_height());
    return { screen->x, sh - screen->y };
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------

static void draw_filled_rect(const unity::Rect& rect, MonoObject* white_tex) {
    if (!white_tex) return;
    gui::draw_texture(rect, white_tex);
}

static void draw_box_outline(float x, float y, float w, float h, float thickness, MonoObject* tex) {
    // Top
    draw_filled_rect({ x, y, w, thickness }, tex);
    // Bottom
    draw_filled_rect({ x, y + h - thickness, w, thickness }, tex);
    // Left
    draw_filled_rect({ x, y, thickness, h }, tex);
    // Right
    draw_filled_rect({ x + w - thickness, y, thickness, h }, tex);
}

static void draw_line(float x1, float y1, float x2, float y2, float thickness, MonoObject* tex) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.001f) return;

    int steps = static_cast<int>(len / 1.0f);
    if (steps < 1) steps = 1;
    if (steps > 2000) steps = 2000;

    float step_x = dx / static_cast<float>(steps);
    float step_y = dy / static_cast<float>(steps);

    for (int i = 0; i < steps; ++i) {
        float px = x1 + step_x * static_cast<float>(i);
        float py = y1 + step_y * static_cast<float>(i);
        draw_filled_rect({ px - thickness * 0.5f, py - thickness * 0.5f, thickness, thickness }, tex);
    }
}

static unity::Color health_color(float ratio) {
    // 1.0 = green, 0.5 = yellow, 0.0 = red
    if (ratio > 0.5f) {
        float t = (ratio - 0.5f) * 2.0f; // 0..1
        return { 1.0f - t, 1.0f, 0.0f, 1.0f }; // yellow -> green
    } else {
        float t = ratio * 2.0f; // 0..1
        return { 1.0f, t, 0.0f, 1.0f }; // red -> yellow
    }
}

// ---------------------------------------------------------------------------
// Format helpers
// ---------------------------------------------------------------------------
static const char* fmt(const char* format, ...) {
    static char buf[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    return buf;
}

// ---------------------------------------------------------------------------
// render
// ---------------------------------------------------------------------------
void render(const std::vector<game_data::GamePlayerData>& players,
            const game_data::GamePlayerData& local_player) {

    if (!s_config.enabled) return;
    if (!s_camera_get_main || !s_camera_w2s) return;

    // Ensure white texture exists (lazy recreate if destroyed)
    MonoObject* white_tex = ensure_white_texture();
    if (!white_tex) return;

    float screen_w = static_cast<float>(gui::screen_width());
    float screen_h = static_cast<float>(gui::screen_height());

    // Save original GUI state
    unity::Color orig_color = gui::get_color();
    int orig_font_size      = gui::get_label_font_size();
    int orig_alignment      = gui::get_label_alignment();

    constexpr float PLAYER_HEIGHT  = 1.8f;
    constexpr float BOX_RATIO      = 0.6f;   // width = height * ratio
    constexpr float BOX_THICKNESS  = 2.0f;
    constexpr float HEALTH_BAR_H   = 4.0f;
    constexpr float HEALTH_BAR_PAD = 2.0f;

    for (const auto& player : players) {
        // Skip dead players
        if (player.is_dead) continue;

        // Skip local player
        if (player.is_local) continue;

        // Determine if enemy or teammate
        bool is_teammate = (player.team == local_player.team) &&
                           (local_player.team != game_data::PlayerTeam::NoTeam);

        // Skip teammates if not showing them
        if (is_teammate && !s_config.show_team) continue;

        // Calculate distance
        float dx = player.world_position.x - local_player.world_position.x;
        float dy = player.world_position.y - local_player.world_position.y;
        float dz = player.world_position.z - local_player.world_position.z;
        float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

        // Skip if beyond max distance
        if (distance > s_config.max_distance) continue;

        // World to screen conversion for feet position
        bool feet_visible = false;
        unity::Vector2 feet_screen = world_to_screen(player.world_position, feet_visible);
        if (!feet_visible) continue;

        // World to screen for head position
        bool head_visible = false;
        unity::Vector2 head_screen = world_to_screen(player.head_position, head_visible);

        // Calculate box dimensions
        float box_height;
        float box_top_y;

        if (head_visible) {
            // Use actual head-to-feet screen distance
            box_height = feet_screen.y - head_screen.y;
            if (box_height < 10.0f) box_height = 10.0f;
            box_top_y = head_screen.y;
        } else {
            // Estimate box height from distance
            if (distance < 1.0f) distance = 1.0f;
            box_height = (screen_h * PLAYER_HEIGHT) / distance;
            if (box_height < 10.0f) box_height = 10.0f;
            box_top_y = feet_screen.y - box_height;
        }

        float box_width = box_height * BOX_RATIO;
        float box_x = feet_screen.x - box_width * 0.5f;
        float box_y = box_top_y;

        // Pick color: red for enemies, green for teammates
        unity::Color player_color = is_teammate
            ? unity::Color(0.0f, 1.0f, 0.0f, 1.0f)
            : unity::Color(1.0f, 0.0f, 0.0f, 1.0f);

        // ---- Box ----
        if (s_config.show_box) {
            gui::set_color(player_color);
            draw_box_outline(box_x, box_y, box_width, box_height, BOX_THICKNESS, white_tex);
        }

        // ---- Name ----
        if (s_config.show_name) {
            gui::set_color(unity::Color::white());
            gui::set_label_font_size(12);
            gui::set_label_alignment(gui::text_anchor::LowerCenter);
            gui::label({ box_x - 20.0f, box_y - 18.0f, box_width + 40.0f, 16.0f },
                       player.name.c_str());
        }

        // ---- Health bar ----
        if (s_config.show_health && player.max_blood > 0) {
            float hp_ratio = static_cast<float>(player.blood) /
                             static_cast<float>(player.max_blood);
            if (hp_ratio < 0.0f) hp_ratio = 0.0f;
            if (hp_ratio > 1.0f) hp_ratio = 1.0f;

            float bar_y = box_y + box_height + HEALTH_BAR_PAD;
            float bar_w = box_width;

            // Background (dark)
            gui::set_color({ 0.0f, 0.0f, 0.0f, 0.7f });
            draw_filled_rect({ box_x, bar_y, bar_w, HEALTH_BAR_H }, white_tex);

            // Filled portion
            gui::set_color(health_color(hp_ratio));
            draw_filled_rect({ box_x, bar_y, bar_w * hp_ratio, HEALTH_BAR_H }, white_tex);
        }

        // ---- Distance ----
        if (s_config.show_distance) {
            gui::set_color(unity::Color::white());
            gui::set_label_font_size(11);
            gui::set_label_alignment(gui::text_anchor::UpperCenter);

            float dist_y = box_y + box_height + HEALTH_BAR_PAD + HEALTH_BAR_H + 2.0f;
            gui::label({ box_x - 20.0f, dist_y, box_width + 40.0f, 16.0f },
                       fmt("%.0fm", distance));
        }

        // ---- Snapline ----
        if (s_config.show_snapline) {
            gui::set_color(player_color);
            draw_line(screen_w * 0.5f, screen_h, feet_screen.x, feet_screen.y, 1.5f, white_tex);
        }
    }

    // Restore original GUI state
    gui::set_color(orig_color);
    gui::set_label_font_size(orig_font_size);
    gui::set_label_alignment(orig_alignment);
}

} // namespace esp
