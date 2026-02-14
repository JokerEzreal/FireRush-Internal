#include "cheats.h"
#include "../mono/mono_api.h"
#include "../mono/mono_types.h"
#include "../unity/unity_classes.h"
#include "../core/game_data.h"
#include "../gui/gui.h"

#include <cstring>
#include <cstdio>
#include <cmath>
#include <string>

namespace cheats {

// ---------------------------------------------------------------------------
// Mono invoke helper
// ---------------------------------------------------------------------------
static MonoObject* invoke_safe(MonoMethod* method, MonoObject* obj = nullptr,
                               void** params = nullptr) {
    if (!method) return nullptr;
    MonoObject* exc = nullptr;
    MonoObject* ret = mono::runtime_invoke(method, obj, params, &exc);
    return exc ? nullptr : ret;
}

// ---------------------------------------------------------------------------
// Cached class / field / method handles
// ---------------------------------------------------------------------------

// PlayerLocal
static MonoClass*      s_PlayerLocal_class   = nullptr;
static MonoMethod*     s_PL_get_Instance     = nullptr;
static MonoClassField* s_PL_motor            = nullptr;
static MonoClassField* s_PL_weaponShot       = nullptr;
static MonoClassField* s_PL_smartCross       = nullptr;
static MonoClassField* s_PL_weaponManager    = nullptr;
static MonoClassField* s_PL_Melee            = nullptr;   // GameObject

// CharacterMotorYS
static MonoClass*      s_MotorYS_class       = nullptr;
static MonoClassField* s_Motor_movement      = nullptr;

// CharacterMotorMovementYS - speed properties (StopPlugins-backed)
static MonoClass*      s_Movement_class      = nullptr;
static MonoMethod*     s_Mov_get_fwd         = nullptr;
static MonoMethod*     s_Mov_set_fwd         = nullptr;
static MonoMethod*     s_Mov_get_side        = nullptr;
static MonoMethod*     s_Mov_set_side        = nullptr;
static MonoMethod*     s_Mov_get_back        = nullptr;
static MonoMethod*     s_Mov_set_back        = nullptr;

// WeaponShotScript
static MonoClass*      s_WSS_class           = nullptr;
static MonoClassField* s_WSS_fireRate        = nullptr;   // float (plain)
static MonoClassField* s_WSS_nextFireTime    = nullptr;   // StopPlugins
static MonoClassField* s_WSS_fireTime        = nullptr;   // StopPlugins (_fireTime)
static MonoClassField* s_WSS_fireCD          = nullptr;   // float
static MonoClassField* s_WSS_ShootBullet     = nullptr;   // StopPlugins
static MonoClassField* s_WSS_reloadA         = nullptr;   // ReloadAmmo
static MonoClassField* s_WSS_fire            = nullptr;   // FireInfo
static MonoMethod*     s_WSS_Fire            = nullptr;   // Fire() - public, the main fire entry point

// ReloadAmmo
static MonoClass*      s_RA_class            = nullptr;
static MonoClassField* s_RA_reload           = nullptr;   // StopPlugins

// MeleeShot (component on PlayerLocal.Melee GameObject)
static MonoClass*      s_MS_class            = nullptr;
static MonoClassField* s_MS_nextShot         = nullptr;   // float
// GetComponent method on GameObject
static MonoMethod*     s_GO_GetComponent     = nullptr;

// SmartCrosshair (recoil + spread - all plain floats on runtime object)
static MonoClass*      s_SC_class            = nullptr;
static MonoClassField* s_SC_shakecCamera     = nullptr;
static MonoClassField* s_SC_shakeHands       = nullptr;
static MonoClassField* s_SC_handsOffset      = nullptr;
static MonoClassField* s_SC_cameraUpDown     = nullptr;
static MonoClassField* s_SC_cameraLR         = nullptr;
static MonoClassField* s_SC_originPointA     = nullptr;
static MonoClassField* s_SC_curveB           = nullptr;
static MonoClassField* s_SC_overallC         = nullptr;

// WeaponManager
static MonoClass*      s_WM_class            = nullptr;
static MonoClassField* s_WM_weaponDict       = nullptr;

// WeaponBaseInfo
static MonoClass*      s_WBI_class           = nullptr;
static MonoClassField* s_WBI_bulletCount     = nullptr;   // StopPlugins
static MonoClassField* s_WBI_reserveBullet   = nullptr;   // StopPlugins
static MonoClassField* s_WBI_maxBullet       = nullptr;   // StopPlugins
static MonoClassField* s_WBI_parseWeaponCfg  = nullptr;   // -> ParseWeaponConfig

// ParseWeaponConfig
static MonoClass*      s_PWC_class           = nullptr;
static MonoClassField* s_PWC_weaponAttribute = nullptr;   // -> WeaponAttributeConfig
static MonoClassField* s_PWC_weaponCrosshair = nullptr;   // -> WeaponScreenCrosshair

// WeaponAttributeConfig
static MonoClass*      s_WAC_class           = nullptr;
static MonoClassField* s_WAC_BulletSpeed     = nullptr;   // float (plain)
static MonoClassField* s_WAC_swithTime       = nullptr;   // StopPlugins
static MonoClassField* s_WAC_reloadTime      = nullptr;   // StopPlugins
static MonoClassField* s_WAC_shootBullet     = nullptr;   // StopPlugins
static MonoClassField* s_WAC_fireRate        = nullptr;   // StopPlugins (_fireRate)
static MonoClassField* s_WAC_attactDelay     = nullptr;   // StopPlugins
static MonoClassField* s_WAC_ValidRange      = nullptr;   // float (plain)
static MonoClassField* s_WAC_Reference       = nullptr;   // float (plain) - base PVE damage
static MonoClassField* s_WAC_HightHurt       = nullptr;   // float (plain) - heavy PVE damage
static MonoClassField* s_WAC_ID              = nullptr;   // int - weapon ID
static MonoClassField* s_WAC_Close           = nullptr;   // float - close range threshold
static MonoClassField* s_WAC_Middle          = nullptr;   // float - mid range threshold
static MonoClassField* s_WAC_Far             = nullptr;   // float - far range threshold
static MonoClassField* s_WAC_CloseFactor     = nullptr;   // float - damage % at close
static MonoClassField* s_WAC_MiddleFactor    = nullptr;   // float - damage % at mid
static MonoClassField* s_WAC_FarFactor       = nullptr;   // float - damage % at far
static MonoClassField* s_WAC_Penetrate       = nullptr;   // bool - enable wall penetration
static MonoClassField* s_WAC_PeneCount       = nullptr;   // StopPlugins - _基础穿透数
static MonoClassField* s_WAC_PeneRange       = nullptr;   // float - PenetrateValidRange

// WeaponScreenCrosshair (weapon config - source of recoil values)
static MonoClass*      s_WSC_class           = nullptr;
static MonoClassField* s_WSC_shakecCamera    = nullptr;
static MonoClassField* s_WSC_shakeHands      = nullptr;
static MonoClassField* s_WSC_handsOffset     = nullptr;
static MonoClassField* s_WSC_cameraUpDown    = nullptr;
static MonoClassField* s_WSC_cameraLR        = nullptr;
static MonoClassField* s_WSC_Standing        = nullptr;
static MonoClassField* s_WSC_Moving          = nullptr;
static MonoClassField* s_WSC_Jumping         = nullptr;
static MonoClassField* s_WSC_Crouching       = nullptr;

// WeaponCrosshairState (spread config per state)
static MonoClass*      s_WCS_class           = nullptr;
static MonoClassField* s_WCS_originPointA    = nullptr;
static MonoClassField* s_WCS_curveB          = nullptr;
static MonoClassField* s_WCS_overallC        = nullptr;

// Player (accessed via PlayerLocal.player)
static MonoClass*      s_Player_class        = nullptr;
static MonoClassField* s_Player_blood        = nullptr;   // int
static MonoClassField* s_Player_maxblood     = nullptr;   // int
static MonoClassField* s_PL_player           = nullptr;   // PlayerLocal -> Player

// WeaponShotPVE (direct fire calling)
static MonoClass*      s_WSP_class            = nullptr;
static MonoMethod*     s_WSP_FireShot         = nullptr;   // FireShot() - protected override
static MonoMethod*     s_WSP_GetInstance      = nullptr;   // UIKQaUOgd25wpfZrq7qc() - static singleton getter

// ServerNet (packet sending)
static MonoClass*      s_ServerNet_class      = nullptr;
static MonoMethod*     s_SN_get_Instance      = nullptr;
static MonoMethod*     s_SN_Send_2            = nullptr;   // Send(CSOperationEnum, Dict<byte,obj>)
static MonoMethod*     s_SN_Send_3            = nullptr;   // Send(CSOperationEnum, Dict<byte,obj>, bool)
static MonoClassField* s_SN_loop              = nullptr;   // Dictionary<byte,object>

// ServerDataTrans (call game's own packet-sending methods directly)
static MonoClass*      s_SDT_class            = nullptr;
static MonoMethod*     s_SDT_KillAllMons      = nullptr;   // 杀死所以怪物() - static, 0 params
static MonoMethod*     s_SDT_PVEServerFire    = nullptr;   // WeaponShotPVE_PVEServerFire(Dict) - static, 1 param

// CSOperationEnum byte constants (literal enum fields, extracted from metadata)
static constexpr uint8_t CS_KillAllMons   = 141;
static constexpr uint8_t CS_AllMonsDamage = 39;
static constexpr uint8_t CS_FirePve       = 129;

// StopPlugins
static MonoClass*      s_SP_class            = nullptr;
static MonoMethod*     s_SP_get_Value        = nullptr;
static MonoMethod*     s_SP_SetValue         = nullptr;

// GlobalManager (static class - need baseCount for ammo offset)
static MonoClass*      s_GM_class            = nullptr;
static MonoClassField* s_GM_baseCount        = nullptr;   // static int

// PlayerMouseLook (aimbot - camera yaw/pitch control)
static MonoClass*      s_PML_class           = nullptr;
static MonoClassField* s_PML_XRotation       = nullptr;   // float
static MonoClassField* s_PML_YRotation       = nullptr;   // float
static MonoClassField* s_PML_m_InitialRotation = nullptr; // Vector2
static MonoClassField* s_PL_mouseX           = nullptr;   // PlayerLocal -> PlayerMouseLook
static MonoClassField* s_PL_mouseY           = nullptr;   // PlayerLocal -> PlayerMouseLook

// DataCenter (channel unlock - level + honor spoof)
static MonoClass*      s_DC_class            = nullptr;
static MonoMethod*     s_DC_get_Player       = nullptr;   // static DataCenter.get_Player()
// Fields resolved at runtime from the returned object (Chinese names)
static MonoClassField* s_Info_juese          = nullptr;   // SC.推送.玩家.信息.角色
static MonoClassField* s_Juese_dengji       = nullptr;   // SC.推送.玩家.角色.等级
static MonoClassField* s_Juese_haoyudu      = nullptr;   // SC.推送.玩家.角色.好誉度
static bool            s_channel_fields_resolved = false;
static int             s_orig_level          = -1;
static int             s_orig_honor          = -1;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static CheatConfig s_config;
static char s_debug_buf[512] = {};

// Speed hack originals
static float s_orig_fwd_speed   = 0;
static float s_orig_side_speed  = 0;
static float s_orig_back_speed  = 0;
static bool  s_speed_cached     = false;

// Bullet speed originals (per WeaponAttributeConfig object)
struct BulletSpeedOrig {
    MonoObject* wac;
    float       orig;
};
static BulletSpeedOrig s_bs_orig[16] = {};
static int  s_bs_orig_count = 0;
static bool s_bs_modified   = false;

// Range hack originals
struct RangeOrig {
    MonoObject* wac;
    float       orig;
};
static RangeOrig s_range_orig[16] = {};
static int  s_range_orig_count = 0;
static bool s_range_modified   = false;

// PVE damage originals (per WeaponAttributeConfig object)
struct DamageOrig {
    MonoObject* wac;
    float       orig_ref;   // WAC.Reference
    float       orig_hh;    // WAC.HightHurt
};
static DamageOrig s_dmg_orig[16] = {};
static int  s_dmg_orig_count = 0;
static bool s_dmg_modified   = false;

// Weapon boost originals (per WeaponAttributeConfig object)
// NOTE: avoid 'far'/'near' as member names - they are Windows macros!
struct BoostOrig {
    MonoObject* wac;
    float close_d, middle_d, far_d;
    float close_f, middle_f, far_f;
    float valid_range, pene_range;
    bool  penetrate;
};
static BoostOrig s_boost_orig[16] = {};
static int  s_boost_orig_count = 0;
static bool s_boost_modified   = false;

// Weapon ID swap originals
struct IdSwapOrig {
    MonoObject* wac;
    int         orig_id;
};
static IdSwapOrig s_id_orig[16] = {};
static int  s_id_orig_count  = 0;
static bool s_id_modified    = false;

// Current weapon ID (updated each frame for menu display)
static int s_current_weapon_id = 0;

// Dict layout constants (same as game_data.cpp, x86 Mono)
static constexpr int DICT_OFF_LINK_SLOTS    = 0x0C;
static constexpr int DICT_OFF_VALUE_SLOTS   = 0x14;
static constexpr int DICT_OFF_TOUCHED_SLOTS = 0x18;
static constexpr int MONO_ARRAY_DATA        = 0x10;
static constexpr int LINK_STRUCT_SIZE       = 8;
static constexpr int HASH_FLAG              = (int)0x80000000;

// ---------------------------------------------------------------------------
// StopPlugins helpers
// ---------------------------------------------------------------------------
static float sp_get_value(MonoObject* sp_obj) {
    if (!sp_obj || !s_SP_get_Value) return 0.0f;
    MonoObject* ret = invoke_safe(s_SP_get_Value, sp_obj);
    if (!ret) return 0.0f;
    return *static_cast<float*>(mono::object_unbox(ret));
}

static void sp_set_value(MonoObject* sp_obj, float value) {
    if (!sp_obj || !s_SP_SetValue) return;
    void* args[1] = { &value };
    invoke_safe(s_SP_SetValue, sp_obj, args);
}

static MonoObject* get_sp_field(MonoObject* parent, MonoClassField* field) {
    if (!parent || !field) return nullptr;
    MonoObject* sp = nullptr;
    mono::field_get_value(parent, field, &sp);
    return sp;
}

// ---------------------------------------------------------------------------
// GlobalManager.baseCount helper
// The game stores bullet counts as (actual_bullets + baseCount) in StopPlugins.
// We must add this offset when setting ammo, otherwise the game sees negative
// bullet counts and disables damage after a while.
// ---------------------------------------------------------------------------
static int get_base_count() {
    if (!s_GM_class || !s_GM_baseCount) return 0;
    if (!mono::class_vtable || !mono::field_static_get_value) return 0;
    MonoDomain* domain = mono::get_root_domain();
    if (!domain) return 0;
    MonoVTable* vt = mono::class_vtable(domain, s_GM_class);
    if (!vt) return 0;
    int bc = 0;
    mono::field_static_get_value(vt, s_GM_baseCount, &bc);
    return bc;
}

// ---------------------------------------------------------------------------
// initialize
// ---------------------------------------------------------------------------
bool initialize() {
    MonoImage* csharp_img = unity::images().assembly_csharp;
    if (!csharp_img) return false;

    // PlayerLocal
    s_PlayerLocal_class = mono::class_from_name(csharp_img, "", "PlayerLocal");
    if (s_PlayerLocal_class) {
        s_PL_get_Instance  = mono::class_get_method_from_name(s_PlayerLocal_class, "get_Instance", 0);
        s_PL_motor         = mono::class_get_field_from_name(s_PlayerLocal_class, "motor");
        s_PL_weaponShot    = mono::class_get_field_from_name(s_PlayerLocal_class, "weaponShot");
        s_PL_smartCross    = mono::class_get_field_from_name(s_PlayerLocal_class, "smartCross");
        s_PL_weaponManager = mono::class_get_field_from_name(s_PlayerLocal_class, "weaponManager");
        s_PL_Melee         = mono::class_get_field_from_name(s_PlayerLocal_class, "Melee");
        s_PL_player        = mono::class_get_field_from_name(s_PlayerLocal_class, "player");
        s_PL_mouseX        = mono::class_get_field_from_name(s_PlayerLocal_class, "mouseX");
        s_PL_mouseY        = mono::class_get_field_from_name(s_PlayerLocal_class, "mouseY");
    }

    // Player
    s_Player_class = mono::class_from_name(csharp_img, "", "Player");
    if (s_Player_class) {
        s_Player_blood    = mono::class_get_field_from_name(s_Player_class, "blood");
        s_Player_maxblood = mono::class_get_field_from_name(s_Player_class, "maxblood");
    }

    // CharacterMotorYS
    s_MotorYS_class = mono::class_from_name(csharp_img, "", "CharacterMotorYS");
    if (s_MotorYS_class) {
        s_Motor_movement = mono::class_get_field_from_name(s_MotorYS_class, "movement");
    }

    // CharacterMotorMovementYS
    s_Movement_class = mono::class_from_name(csharp_img, "", "CharacterMotorMovementYS");
    if (s_Movement_class) {
        s_Mov_get_fwd  = mono::class_get_method_from_name(s_Movement_class, "get_maxForwardSpeed", 0);
        s_Mov_set_fwd  = mono::class_get_method_from_name(s_Movement_class, "set_maxForwardSpeed", 1);
        s_Mov_get_side = mono::class_get_method_from_name(s_Movement_class, "get_maxSidewaysSpeed", 0);
        s_Mov_set_side = mono::class_get_method_from_name(s_Movement_class, "set_maxSidewaysSpeed", 1);
        s_Mov_get_back = mono::class_get_method_from_name(s_Movement_class, "get_maxBackwardsSpeed", 0);
        s_Mov_set_back = mono::class_get_method_from_name(s_Movement_class, "set_maxBackwardsSpeed", 1);
    }

    // WeaponShotScript
    s_WSS_class = mono::class_from_name(csharp_img, "", "WeaponShotScript");
    if (s_WSS_class) {
        s_WSS_fireRate     = mono::class_get_field_from_name(s_WSS_class, "fireRate");
        s_WSS_nextFireTime = mono::class_get_field_from_name(s_WSS_class, "_nextFireTime");
        s_WSS_fireTime     = mono::class_get_field_from_name(s_WSS_class, "_fireTime");
        s_WSS_fireCD       = mono::class_get_field_from_name(s_WSS_class, "fireCD");
        s_WSS_ShootBullet  = mono::class_get_field_from_name(s_WSS_class, "_ShootBullet");
        s_WSS_reloadA      = mono::class_get_field_from_name(s_WSS_class, "reloadA");
        s_WSS_fire         = mono::class_get_field_from_name(s_WSS_class, "fire");
        s_WSS_Fire         = mono::class_get_method_from_name(s_WSS_class, "Fire", 0);
    }

    // ReloadAmmo
    s_RA_class = mono::class_from_name(csharp_img, "", "ReloadAmmo");
    if (s_RA_class) {
        s_RA_reload = mono::class_get_field_from_name(s_RA_class, "_reload");
    }

    // MeleeShot (accessed via GetComponent on Melee GameObject)
    s_MS_class = mono::class_from_name(csharp_img, "", "MeleeShot");
    if (s_MS_class) {
        s_MS_nextShot = mono::class_get_field_from_name(s_MS_class, "nextShot");
    }

    // Cache GetComponent(Type) on UnityEngine.GameObject
    MonoClass* go_class = unity::classes().GameObject;
    if (go_class) {
        s_GO_GetComponent = mono::class_get_method_from_name(go_class, "GetComponent", 1);
    }

    // SmartCrosshair
    s_SC_class = mono::class_from_name(csharp_img, "", "SmartCrosshair");
    if (s_SC_class) {
        s_SC_shakecCamera = mono::class_get_field_from_name(s_SC_class, "shakecCamera");
        s_SC_shakeHands   = mono::class_get_field_from_name(s_SC_class, "shakeHands");
        s_SC_handsOffset  = mono::class_get_field_from_name(s_SC_class, "handsOffset");
        s_SC_cameraUpDown = mono::class_get_field_from_name(s_SC_class, "cameraUpDown");
        s_SC_cameraLR     = mono::class_get_field_from_name(s_SC_class, "cameraLR");
        s_SC_originPointA = mono::class_get_field_from_name(s_SC_class, "originPointA");
        s_SC_curveB       = mono::class_get_field_from_name(s_SC_class, "curveB");
        s_SC_overallC     = mono::class_get_field_from_name(s_SC_class, "overallC");
    }

    // WeaponManager
    s_WM_class = mono::class_from_name(csharp_img, "", "WeaponManager");
    if (s_WM_class) {
        s_WM_weaponDict = mono::class_get_field_from_name(s_WM_class, "weaponDict");
    }

    // WeaponBaseInfo
    s_WBI_class = mono::class_from_name(csharp_img, "", "WeaponBaseInfo");
    if (s_WBI_class) {
        s_WBI_bulletCount   = mono::class_get_field_from_name(s_WBI_class, "_bulletCount");
        s_WBI_reserveBullet = mono::class_get_field_from_name(s_WBI_class, "_reserveBullet");
        s_WBI_maxBullet     = mono::class_get_field_from_name(s_WBI_class, "_maxBullet");
        s_WBI_parseWeaponCfg = mono::class_get_field_from_name(s_WBI_class, "parseWeaponConfig");
    }

    // ParseWeaponConfig
    s_PWC_class = mono::class_from_name(csharp_img, "", "ParseWeaponConfig");
    if (s_PWC_class) {
        s_PWC_weaponAttribute = mono::class_get_field_from_name(s_PWC_class, "weaponAttribute");
        s_PWC_weaponCrosshair = mono::class_get_field_from_name(s_PWC_class, "weaponCrosshair");
    }

    // WeaponAttributeConfig
    s_WAC_class = mono::class_from_name(csharp_img, "", "WeaponAttributeConfig");
    if (s_WAC_class) {
        s_WAC_BulletSpeed  = mono::class_get_field_from_name(s_WAC_class, "BulletSpeed");
        s_WAC_swithTime    = mono::class_get_field_from_name(s_WAC_class, "swithTime");
        s_WAC_reloadTime   = mono::class_get_field_from_name(s_WAC_class, "reloadTime");
        s_WAC_shootBullet  = mono::class_get_field_from_name(s_WAC_class, "shootBullet");
        s_WAC_fireRate     = mono::class_get_field_from_name(s_WAC_class, "_fireRate");
        s_WAC_attactDelay  = mono::class_get_field_from_name(s_WAC_class, "attactDelay");
        s_WAC_ValidRange   = mono::class_get_field_from_name(s_WAC_class, "ValidRange");
        s_WAC_Reference    = mono::class_get_field_from_name(s_WAC_class, "Reference");
        s_WAC_HightHurt    = mono::class_get_field_from_name(s_WAC_class, "HightHurt");
        s_WAC_ID           = mono::class_get_field_from_name(s_WAC_class, "ID");
        s_WAC_Close        = mono::class_get_field_from_name(s_WAC_class, "Close");
        s_WAC_Middle       = mono::class_get_field_from_name(s_WAC_class, "Middle");
        s_WAC_Far          = mono::class_get_field_from_name(s_WAC_class, "Far");
        s_WAC_CloseFactor  = mono::class_get_field_from_name(s_WAC_class, "CloseFactor");
        s_WAC_MiddleFactor = mono::class_get_field_from_name(s_WAC_class, "MiddleFactor");
        s_WAC_FarFactor    = mono::class_get_field_from_name(s_WAC_class, "FarFactor");
        s_WAC_Penetrate    = mono::class_get_field_from_name(s_WAC_class, "Penetrate");
        s_WAC_PeneRange    = mono::class_get_field_from_name(s_WAC_class, "PenetrateValidRange");

        // _基础穿透数 - find by iterating (Chinese UTF-8 field name)
        if (mono::class_get_fields && mono::field_get_name) {
            void* iter = nullptr;
            MonoClassField* f = nullptr;
            while ((f = mono::class_get_fields(s_WAC_class, &iter)) != nullptr) {
                const char* fn = mono::field_get_name(f);
                if (!fn) continue;
                // UTF-8 for _基础穿透数: 5F E5 9F BA E7 A1 80 E7 A9 BF E9 80 8F E6 95 B0
                if (fn[0] == '_' && strstr(fn + 1, "\xe5\x9f\xba\xe7\xa1\x80\xe7\xa9\xbf\xe9\x80\x8f\xe6\x95\xb0")) {
                    s_WAC_PeneCount = f;
                    break;
                }
            }
        }
    }

    // WeaponScreenCrosshair
    s_WSC_class = mono::class_from_name(csharp_img, "", "WeaponScreenCrosshair");
    if (s_WSC_class) {
        s_WSC_shakecCamera = mono::class_get_field_from_name(s_WSC_class, "shakecCamera");
        s_WSC_shakeHands   = mono::class_get_field_from_name(s_WSC_class, "shakeHands");
        s_WSC_handsOffset  = mono::class_get_field_from_name(s_WSC_class, "handsOffset");
        s_WSC_cameraUpDown = mono::class_get_field_from_name(s_WSC_class, "cameraUpDown");
        s_WSC_cameraLR     = mono::class_get_field_from_name(s_WSC_class, "cameraLR");
        s_WSC_Standing     = mono::class_get_field_from_name(s_WSC_class, "Standing");
        s_WSC_Moving       = mono::class_get_field_from_name(s_WSC_class, "Moving");
        s_WSC_Jumping      = mono::class_get_field_from_name(s_WSC_class, "Jumping");
        s_WSC_Crouching    = mono::class_get_field_from_name(s_WSC_class, "Crouching");
    }

    // WeaponCrosshairState
    s_WCS_class = mono::class_from_name(csharp_img, "", "WeaponCrosshairState");
    if (s_WCS_class) {
        s_WCS_originPointA = mono::class_get_field_from_name(s_WCS_class, "originPointA");
        s_WCS_curveB       = mono::class_get_field_from_name(s_WCS_class, "curveB");
        s_WCS_overallC     = mono::class_get_field_from_name(s_WCS_class, "overallC");
    }

    // StopPlugins
    s_SP_class = mono::class_from_name(csharp_img, "", "StopPlugins");
    if (s_SP_class) {
        s_SP_get_Value = mono::class_get_method_from_name(s_SP_class, "get_Value", 0);
        s_SP_SetValue  = mono::class_get_method_from_name(s_SP_class, "SetValue", 1);
    }

    // GlobalManager (static class - baseCount is the anti-cheat offset added to bullet counts)
    s_GM_class = mono::class_from_name(csharp_img, "", "GlobalManager");
    if (s_GM_class) {
        s_GM_baseCount = mono::class_get_field_from_name(s_GM_class, "baseCount");
    }

    // PlayerMouseLook (aimbot camera control)
    s_PML_class = mono::class_from_name(csharp_img, "", "PlayerMouseLook");
    if (s_PML_class) {
        s_PML_XRotation       = mono::class_get_field_from_name(s_PML_class, "XRotation");
        s_PML_YRotation       = mono::class_get_field_from_name(s_PML_class, "YRotation");
        s_PML_m_InitialRotation = mono::class_get_field_from_name(s_PML_class, "m_InitialRotation");
    }

    // DataCenter (channel unlock)
    s_DC_class = mono::class_from_name(csharp_img, "", "DataCenter");
    if (s_DC_class) {
        s_DC_get_Player = mono::class_get_method_from_name(s_DC_class, "get_Player", 0);
    }

    // WeaponShotPVE (direct fire calling for PVE auto-fire)
    s_WSP_class = mono::class_from_name(csharp_img, "", "WeaponShotPVE");
    if (s_WSP_class) {
        s_WSP_FireShot = mono::class_get_method_from_name(s_WSP_class, "FireShot", 0);
        // Singleton getter: UIKQaUOgd25wpfZrq7qc() -> returns static instance
        s_WSP_GetInstance = mono::class_get_method_from_name(s_WSP_class, "UIKQaUOgd25wpfZrq7qc", 0);
    }

    // ServerNet (packet sending)
    s_ServerNet_class = mono::class_from_name(csharp_img, "", "ServerNet");
    if (s_ServerNet_class) {
        s_SN_get_Instance = mono::class_get_method_from_name(s_ServerNet_class, "get_Instance", 0);
        s_SN_Send_2       = mono::class_get_method_from_name(s_ServerNet_class, "Send", 2);
        s_SN_Send_3       = mono::class_get_method_from_name(s_ServerNet_class, "Send", 3);
        s_SN_loop         = mono::class_get_field_from_name(s_ServerNet_class, "loop");
    }

    // ServerDataTrans (static class - call game's own packet methods)
    s_SDT_class = mono::class_from_name(csharp_img, "", "ServerDataTrans");
    if (s_SDT_class) {
        // Find methods by iterating (Chinese names may have encoding issues with direct lookup)
        s_SDT_PVEServerFire = mono::class_get_method_from_name(s_SDT_class, "WeaponShotPVE_PVEServerFire", 1);

        // Find 杀死所以怪物 by iterating all methods (Chinese UTF-8 name)
        if (mono::class_get_methods && mono::method_get_name) {
            void* iter = nullptr;
            MonoMethod* m = nullptr;
            while ((m = mono::class_get_methods(s_SDT_class, &iter)) != nullptr) {
                const char* mname = mono::method_get_name(m);
                if (!mname) continue;
                // Match the Chinese method name bytes for 杀死所以怪物
                // UTF-8: E6 9D 80 E6 AD BB E6 89 80 E4 BB A5 E6 80 AA E7 89 A9
                if (strstr(mname, "\xe6\x9d\x80\xe6\xad\xbb\xe6\x89\x80\xe4\xbb\xa5\xe6\x80\xaa\xe7\x89\xa9")) {
                    s_SDT_KillAllMons = m;
                    break;
                }
            }
        }
    }

    return s_PL_get_Instance != nullptr;
}

// ---------------------------------------------------------------------------
// Helper: get PlayerLocal instance
// ---------------------------------------------------------------------------
static MonoObject* get_player_local() {
    if (!s_PL_get_Instance) return nullptr;
    return invoke_safe(s_PL_get_Instance);
}

// ---------------------------------------------------------------------------
// Speed hack
// ---------------------------------------------------------------------------
static void apply_speed_hack(MonoObject* pl) {
    if (!s_PL_motor || !s_Motor_movement) return;

    MonoObject* motor = nullptr;
    mono::field_get_value(pl, s_PL_motor, &motor);
    if (!motor) return;

    MonoObject* movement = nullptr;
    mono::field_get_value(motor, s_Motor_movement, &movement);
    if (!movement) return;

    if (s_config.speed_hack) {
        if (!s_speed_cached) {
            if (s_Mov_get_fwd) {
                MonoObject* r = invoke_safe(s_Mov_get_fwd, movement);
                if (r) s_orig_fwd_speed = *static_cast<float*>(mono::object_unbox(r));
            }
            if (s_Mov_get_side) {
                MonoObject* r = invoke_safe(s_Mov_get_side, movement);
                if (r) s_orig_side_speed = *static_cast<float*>(mono::object_unbox(r));
            }
            if (s_Mov_get_back) {
                MonoObject* r = invoke_safe(s_Mov_get_back, movement);
                if (r) s_orig_back_speed = *static_cast<float*>(mono::object_unbox(r));
            }
            if (s_orig_fwd_speed > 0.1f)
                s_speed_cached = true;
        }

        if (s_speed_cached) {
            float fwd  = s_orig_fwd_speed  * s_config.speed_multiplier;
            float side = s_orig_side_speed  * s_config.speed_multiplier;
            float back = s_orig_back_speed  * s_config.speed_multiplier;
            if (s_Mov_set_fwd)  { void* a[1] = {&fwd};  invoke_safe(s_Mov_set_fwd, movement, a); }
            if (s_Mov_set_side) { void* a[1] = {&side}; invoke_safe(s_Mov_set_side, movement, a); }
            if (s_Mov_set_back) { void* a[1] = {&back}; invoke_safe(s_Mov_set_back, movement, a); }
        }
    } else if (s_speed_cached) {
        if (s_Mov_set_fwd)  { void* a[1] = {&s_orig_fwd_speed};  invoke_safe(s_Mov_set_fwd, movement, a); }
        if (s_Mov_set_side) { void* a[1] = {&s_orig_side_speed}; invoke_safe(s_Mov_set_side, movement, a); }
        if (s_Mov_set_back) { void* a[1] = {&s_orig_back_speed}; invoke_safe(s_Mov_set_back, movement, a); }
        s_speed_cached = false;
    }
}

// ---------------------------------------------------------------------------
// Rapid fire: zero _nextFireTime (for guns)
// Also handles multi-bullet + instant reload at runtime level
// ---------------------------------------------------------------------------
static void apply_rapid_fire_and_runtime(MonoObject* pl) {
    if (!s_PL_weaponShot) return;

    MonoObject* wss = nullptr;
    mono::field_get_value(pl, s_PL_weaponShot, &wss);
    if (!wss) return;

    // Rapid fire: zero _nextFireTime cooldown (mainly for guns)
    if (s_config.rapid_fire && s_WSS_nextFireTime) {
        MonoObject* sp_next = get_sp_field(wss, s_WSS_nextFireTime);
        if (sp_next) sp_set_value(sp_next, 0.0f);
    }

    // Multi-bullet: set _ShootBullet on runtime WSS
    if (s_config.multi_bullet && s_WSS_ShootBullet) {
        MonoObject* sp_sb = get_sp_field(wss, s_WSS_ShootBullet);
        if (sp_sb) sp_set_value(sp_sb, static_cast<float>(s_config.multi_bullet_count));
    }

    // Instant reload: zero ReloadAmmo._reload at runtime
    if (s_config.instant_reload && s_WSS_reloadA && s_RA_reload) {
        MonoObject* ra = nullptr;
        mono::field_get_value(wss, s_WSS_reloadA, &ra);
        if (ra) {
            MonoObject* sp_reload = get_sp_field(ra, s_RA_reload);
            if (sp_reload) sp_set_value(sp_reload, 0.0f);
        }
    }
}

// ---------------------------------------------------------------------------
// Fast knife: speed up melee attacks
// - WeaponShotScript: zero _nextFireTime (cooldown between attacks)
// - MeleeShot (via GetComponent on Melee GO): set nextShot to huge value
//   (nextShot is the attack window END TIME; attack proceeds while
//    nextShot > Time.time, so a large value keeps the window open)
// NOTE: Do NOT zero fireRate/fireCD/_fireTime — that breaks attack logic.
// ---------------------------------------------------------------------------
static void apply_fast_knife(MonoObject* pl) {
    if (!s_config.fast_knife) return;
    if (!s_PL_weaponShot) return;

    MonoObject* wss = nullptr;
    mono::field_get_value(pl, s_PL_weaponShot, &wss);
    if (!wss) return;

    // Zero _nextFireTime (StopPlugins) — removes cooldown between attacks
    if (s_WSS_nextFireTime) {
        MonoObject* sp = get_sp_field(wss, s_WSS_nextFireTime);
        if (sp) sp_set_value(sp, 0.0f);
    }

    // Access MeleeShot component via PlayerLocal.Melee -> GetComponent(MeleeShot)
    if (s_PL_Melee && s_MS_class && s_MS_nextShot && s_GO_GetComponent &&
        mono::class_get_type && mono::type_get_object) {

        MonoObject* melee_go = nullptr;
        mono::field_get_value(pl, s_PL_Melee, &melee_go);
        if (melee_go) {
            MonoDomain* domain = mono::get_root_domain();
            if (domain) {
                MonoType* ms_type = mono::class_get_type(s_MS_class);
                if (ms_type) {
                    MonoObject* type_obj = mono::type_get_object(domain, ms_type);
                    if (type_obj) {
                        void* args[1] = { type_obj };
                        MonoObject* melee_shot = invoke_safe(s_GO_GetComponent, melee_go, args);
                        if (melee_shot) {
                            // Set nextShot far into the future so the attack window stays open
                            float big = 999999.0f;
                            mono::field_set_value(melee_shot, s_MS_nextShot, &big);
                        }
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Bullet speed / range original tracking helpers
// ---------------------------------------------------------------------------
static float find_bs_orig(MonoObject* wac) {
    for (int i = 0; i < s_bs_orig_count; i++) {
        if (s_bs_orig[i].wac == wac) return s_bs_orig[i].orig;
    }
    return -1.0f;
}

static void store_bs_orig(MonoObject* wac, float orig) {
    for (int i = 0; i < s_bs_orig_count; i++) {
        if (s_bs_orig[i].wac == wac) return;
    }
    if (s_bs_orig_count < 16) {
        s_bs_orig[s_bs_orig_count].wac  = wac;
        s_bs_orig[s_bs_orig_count].orig = orig;
        s_bs_orig_count++;
    }
}

static float find_range_orig(MonoObject* wac) {
    for (int i = 0; i < s_range_orig_count; i++) {
        if (s_range_orig[i].wac == wac) return s_range_orig[i].orig;
    }
    return -1.0f;
}

static void store_range_orig(MonoObject* wac, float orig) {
    for (int i = 0; i < s_range_orig_count; i++) {
        if (s_range_orig[i].wac == wac) return;
    }
    if (s_range_orig_count < 16) {
        s_range_orig[s_range_orig_count].wac  = wac;
        s_range_orig[s_range_orig_count].orig = orig;
        s_range_orig_count++;
    }
}

static bool find_dmg_orig(MonoObject* wac, float& ref_out, float& hh_out) {
    for (int i = 0; i < s_dmg_orig_count; i++) {
        if (s_dmg_orig[i].wac == wac) {
            ref_out = s_dmg_orig[i].orig_ref;
            hh_out  = s_dmg_orig[i].orig_hh;
            return true;
        }
    }
    return false;
}

static void store_dmg_orig(MonoObject* wac, float orig_ref, float orig_hh) {
    for (int i = 0; i < s_dmg_orig_count; i++) {
        if (s_dmg_orig[i].wac == wac) return;
    }
    if (s_dmg_orig_count < 16) {
        s_dmg_orig[s_dmg_orig_count].wac      = wac;
        s_dmg_orig[s_dmg_orig_count].orig_ref  = orig_ref;
        s_dmg_orig[s_dmg_orig_count].orig_hh   = orig_hh;
        s_dmg_orig_count++;
    }
}

// ---------------------------------------------------------------------------
// Weapon boost originals helpers
// ---------------------------------------------------------------------------
static bool find_boost_orig(MonoObject* wac, BoostOrig& out) {
    for (int i = 0; i < s_boost_orig_count; i++) {
        if (s_boost_orig[i].wac == wac) { out = s_boost_orig[i]; return true; }
    }
    return false;
}

static void store_boost_orig(MonoObject* wac, const BoostOrig& orig) {
    for (int i = 0; i < s_boost_orig_count; i++) {
        if (s_boost_orig[i].wac == wac) return;
    }
    if (s_boost_orig_count < 16) {
        s_boost_orig[s_boost_orig_count] = orig;
        s_boost_orig_count++;
    }
}

static int find_id_orig(MonoObject* wac) {
    for (int i = 0; i < s_id_orig_count; i++) {
        if (s_id_orig[i].wac == wac) return s_id_orig[i].orig_id;
    }
    return -1;
}

static void store_id_orig(MonoObject* wac, int orig_id) {
    for (int i = 0; i < s_id_orig_count; i++) {
        if (s_id_orig[i].wac == wac) return;
    }
    if (s_id_orig_count < 16) {
        s_id_orig[s_id_orig_count].wac = wac;
        s_id_orig[s_id_orig_count].orig_id = orig_id;
        s_id_orig_count++;
    }
}

// ---------------------------------------------------------------------------
// Crosshair helpers
// ---------------------------------------------------------------------------
static void zero_crosshair_state(MonoObject* state_obj) {
    if (!state_obj) return;
    float zero = 0.0f;
    if (s_WCS_originPointA) mono::field_set_value(state_obj, s_WCS_originPointA, &zero);
    if (s_WCS_curveB)       mono::field_set_value(state_obj, s_WCS_curveB, &zero);
    if (s_WCS_overallC)     mono::field_set_value(state_obj, s_WCS_overallC, &zero);
}

static void zero_screen_crosshair_recoil(MonoObject* wsc) {
    if (!wsc) return;
    float zero = 0.0f;
    if (s_WSC_shakecCamera) mono::field_set_value(wsc, s_WSC_shakecCamera, &zero);
    if (s_WSC_shakeHands)   mono::field_set_value(wsc, s_WSC_shakeHands, &zero);
    if (s_WSC_handsOffset)  mono::field_set_value(wsc, s_WSC_handsOffset, &zero);
    if (s_WSC_cameraUpDown) mono::field_set_value(wsc, s_WSC_cameraUpDown, &zero);
    if (s_WSC_cameraLR)     mono::field_set_value(wsc, s_WSC_cameraLR, &zero);
}

static void zero_screen_crosshair_spread(MonoObject* wsc) {
    if (!wsc) return;
    MonoObject* state = nullptr;
    if (s_WSC_Standing) {
        mono::field_get_value(wsc, s_WSC_Standing, &state);
        zero_crosshair_state(state);
    }
    if (s_WSC_Moving) {
        mono::field_get_value(wsc, s_WSC_Moving, &state);
        zero_crosshair_state(state);
    }
    if (s_WSC_Jumping) {
        mono::field_get_value(wsc, s_WSC_Jumping, &state);
        zero_crosshair_state(state);
    }
    if (s_WSC_Crouching) {
        mono::field_get_value(wsc, s_WSC_Crouching, &state);
        zero_crosshair_state(state);
    }
}

// ---------------------------------------------------------------------------
// Weapon mods: iterates weaponDict once per frame.
// Handles: infinite ammo, bullet speed, no recoil/spread, instant switch,
//          instant reload, multi-bullet, fast knife config, range hack.
// ---------------------------------------------------------------------------
static void apply_weapon_mods(MonoObject* pl) {
    bool need_ammo    = s_config.infinite_ammo;
    bool need_bs      = s_config.bullet_speed;
    bool need_recoil  = s_config.no_recoil;
    bool need_spread  = s_config.no_spread;
    bool need_switch  = s_config.instant_switch;
    bool need_reload  = s_config.instant_reload;
    bool need_multi   = s_config.multi_bullet;
    bool need_fknife  = s_config.fast_knife;
    bool need_range   = s_config.range_hack;
    bool need_dmg     = s_config.pve_damage_boost;
    bool need_boost   = s_config.weapon_boost;
    bool need_idswap  = s_config.weapon_id_swap && s_config.weapon_target_id > 0;

    // Restore bullet speed originals if disabled
    if (!need_bs && s_bs_modified) {
        for (int i = 0; i < s_bs_orig_count; i++) {
            MonoObject* wac = s_bs_orig[i].wac;
            if (wac && s_WAC_BulletSpeed) {
                mono::field_set_value(wac, s_WAC_BulletSpeed, &s_bs_orig[i].orig);
            }
        }
        s_bs_orig_count = 0;
        s_bs_modified = false;
    }

    // Restore range originals if disabled
    if (!need_range && s_range_modified) {
        for (int i = 0; i < s_range_orig_count; i++) {
            MonoObject* wac = s_range_orig[i].wac;
            if (wac && s_WAC_ValidRange) {
                mono::field_set_value(wac, s_WAC_ValidRange, &s_range_orig[i].orig);
            }
        }
        s_range_orig_count = 0;
        s_range_modified = false;
    }

    // Restore PVE damage originals if disabled
    if (!need_dmg && s_dmg_modified) {
        for (int i = 0; i < s_dmg_orig_count; i++) {
            MonoObject* wac = s_dmg_orig[i].wac;
            if (wac) {
                if (s_WAC_Reference) mono::field_set_value(wac, s_WAC_Reference, &s_dmg_orig[i].orig_ref);
                if (s_WAC_HightHurt) mono::field_set_value(wac, s_WAC_HightHurt, &s_dmg_orig[i].orig_hh);
            }
        }
        s_dmg_orig_count = 0;
        s_dmg_modified = false;
    }

    // Restore weapon boost originals if disabled
    if (!need_boost && s_boost_modified) {
        for (int i = 0; i < s_boost_orig_count; i++) {
            MonoObject* wac = s_boost_orig[i].wac;
            if (!wac) continue;
            if (s_WAC_Close)        mono::field_set_value(wac, s_WAC_Close, &s_boost_orig[i].close_d);
            if (s_WAC_Middle)       mono::field_set_value(wac, s_WAC_Middle, &s_boost_orig[i].middle_d);
            if (s_WAC_Far)          mono::field_set_value(wac, s_WAC_Far, &s_boost_orig[i].far_d);
            if (s_WAC_CloseFactor)  mono::field_set_value(wac, s_WAC_CloseFactor, &s_boost_orig[i].close_f);
            if (s_WAC_MiddleFactor) mono::field_set_value(wac, s_WAC_MiddleFactor, &s_boost_orig[i].middle_f);
            if (s_WAC_FarFactor)    mono::field_set_value(wac, s_WAC_FarFactor, &s_boost_orig[i].far_f);
            if (s_WAC_ValidRange)   mono::field_set_value(wac, s_WAC_ValidRange, &s_boost_orig[i].valid_range);
            if (s_WAC_Penetrate)    mono::field_set_value(wac, s_WAC_Penetrate, &s_boost_orig[i].penetrate);
            if (s_WAC_PeneRange)    mono::field_set_value(wac, s_WAC_PeneRange, &s_boost_orig[i].pene_range);
        }
        s_boost_orig_count = 0;
        s_boost_modified = false;
    }

    // Restore weapon ID originals if disabled
    if (!need_idswap && s_id_modified) {
        for (int i = 0; i < s_id_orig_count; i++) {
            MonoObject* wac = s_id_orig[i].wac;
            if (wac && s_WAC_ID)
                mono::field_set_value(wac, s_WAC_ID, &s_id_orig[i].orig_id);
        }
        s_id_orig_count = 0;
        s_id_modified = false;
    }

    if (!need_ammo && !need_bs && !need_recoil && !need_spread &&
        !need_switch && !need_reload && !need_multi && !need_fknife &&
        !need_range && !need_dmg && !need_boost && !need_idswap) return;
    if (!s_PL_weaponManager || !s_WM_weaponDict) return;

    MonoObject* wm = nullptr;
    mono::field_get_value(pl, s_PL_weaponManager, &wm);
    if (!wm) return;

    MonoObject* dict = nullptr;
    mono::field_get_value(wm, s_WM_weaponDict, &dict);
    if (!dict) return;

    char* dict_ptr = reinterpret_cast<char*>(dict);
    int touched = *reinterpret_cast<int*>(dict_ptr + DICT_OFF_TOUCHED_SLOTS);
    if (touched <= 0) return;

    char* link_arr  = *reinterpret_cast<char**>(dict_ptr + DICT_OFF_LINK_SLOTS);
    char* value_arr = *reinterpret_cast<char**>(dict_ptr + DICT_OFF_VALUE_SLOTS);
    if (!link_arr || !value_arr) return;

    char* link_data  = link_arr  + MONO_ARRAY_DATA;
    char* value_data = value_arr + MONO_ARRAY_DATA;

    for (int i = 0; i < touched && i < 16; ++i) {
        int hash_code = *reinterpret_cast<int*>(link_data + i * LINK_STRUCT_SIZE);
        if ((hash_code & HASH_FLAG) == 0) continue;

        MonoObject* wbi = *reinterpret_cast<MonoObject**>(value_data + i * sizeof(void*));
        if (!wbi) continue;

        // --- Infinite ammo ---
        // The game stores bullet counts as (actual + GlobalManager.baseCount).
        // We must add baseCount, otherwise the game interprets raw 999 as
        // (999 - baseCount) = negative → eventually disables damage.
        if (need_ammo) {
            float ammo_val = 999.0f + static_cast<float>(get_base_count());
            MonoObject* sp_count = get_sp_field(wbi, s_WBI_bulletCount);
            if (sp_count) sp_set_value(sp_count, ammo_val);
            MonoObject* sp_reserve = get_sp_field(wbi, s_WBI_reserveBullet);
            if (sp_reserve) sp_set_value(sp_reserve, ammo_val);
        }

        // Get ParseWeaponConfig
        MonoObject* pwc = nullptr;
        if (s_WBI_parseWeaponCfg) {
            mono::field_get_value(wbi, s_WBI_parseWeaponCfg, &pwc);
        }
        if (!pwc) continue;

        // Get WeaponAttributeConfig
        MonoObject* wac = nullptr;
        if (s_PWC_weaponAttribute) {
            mono::field_get_value(pwc, s_PWC_weaponAttribute, &wac);
        }

        if (wac) {
            // --- Bullet speed ---
            if (need_bs && s_WAC_BulletSpeed) {
                float current_bs = 0;
                mono::field_get_value(wac, s_WAC_BulletSpeed, &current_bs);
                float orig = find_bs_orig(wac);
                if (orig < 0) { store_bs_orig(wac, current_bs); orig = current_bs; }
                float new_bs = orig * s_config.bullet_speed_mul;
                mono::field_set_value(wac, s_WAC_BulletSpeed, &new_bs);
                s_bs_modified = true;
            }

            // --- Instant switch ---
            if (need_switch && s_WAC_swithTime) {
                MonoObject* sp_sw = get_sp_field(wac, s_WAC_swithTime);
                if (sp_sw) sp_set_value(sp_sw, 0.0f);
            }

            // --- Instant reload (config level) ---
            if (need_reload && s_WAC_reloadTime) {
                MonoObject* sp_rt = get_sp_field(wac, s_WAC_reloadTime);
                if (sp_rt) sp_set_value(sp_rt, 0.0f);
            }

            // --- Multi-bullet (config level) ---
            if (need_multi && s_WAC_shootBullet) {
                MonoObject* sp_sb = get_sp_field(wac, s_WAC_shootBullet);
                if (sp_sb) sp_set_value(sp_sb, static_cast<float>(s_config.multi_bullet_count));
            }

            // --- Fast knife: minimize _fireRate and attactDelay at config level ---
            if (need_fknife) {
                if (s_WAC_fireRate) {
                    MonoObject* sp_fr = get_sp_field(wac, s_WAC_fireRate);
                    if (sp_fr) sp_set_value(sp_fr, 0.01f); // near-zero but not zero to avoid div/0
                }
                if (s_WAC_attactDelay) {
                    MonoObject* sp_ad = get_sp_field(wac, s_WAC_attactDelay);
                    if (sp_ad) sp_set_value(sp_ad, 0.0f);
                }
            }

            // --- Range hack: multiply ValidRange ---
            if (need_range && s_WAC_ValidRange) {
                float current_range = 0;
                mono::field_get_value(wac, s_WAC_ValidRange, &current_range);
                float orig = find_range_orig(wac);
                if (orig < 0) { store_range_orig(wac, current_range); orig = current_range; }
                float new_range = orig * s_config.range_multiplier;
                mono::field_set_value(wac, s_WAC_ValidRange, &new_range);
                s_range_modified = true;
            }

            // --- PVE damage boost: multiply Reference and HightHurt ---
            if (need_dmg && (s_WAC_Reference || s_WAC_HightHurt)) {
                float cur_ref = 0, cur_hh = 0;
                if (s_WAC_Reference) mono::field_get_value(wac, s_WAC_Reference, &cur_ref);
                if (s_WAC_HightHurt) mono::field_get_value(wac, s_WAC_HightHurt, &cur_hh);

                float orig_ref = 0, orig_hh = 0;
                if (!find_dmg_orig(wac, orig_ref, orig_hh)) {
                    store_dmg_orig(wac, cur_ref, cur_hh);
                    orig_ref = cur_ref;
                    orig_hh  = cur_hh;
                }

                float new_ref = orig_ref * s_config.pve_damage_mul;
                float new_hh  = orig_hh  * s_config.pve_damage_mul;
                if (s_WAC_Reference) mono::field_set_value(wac, s_WAC_Reference, &new_ref);
                if (s_WAC_HightHurt) mono::field_set_value(wac, s_WAC_HightHurt, &new_hh);
                s_dmg_modified = true;
            }

            // --- Weapon boost: max distance factors + penetration ---
            if (need_boost) {
                // Save originals
                BoostOrig bo;
                if (!find_boost_orig(wac, bo)) {
                    bo.wac = wac;
                    bo.close_d = 0; bo.middle_d = 0; bo.far_d = 0;
                    bo.close_f = 0; bo.middle_f = 0; bo.far_f = 0;
                    bo.valid_range = 0; bo.pene_range = 0; bo.penetrate = false;
                    if (s_WAC_Close)        mono::field_get_value(wac, s_WAC_Close, &bo.close_d);
                    if (s_WAC_Middle)       mono::field_get_value(wac, s_WAC_Middle, &bo.middle_d);
                    if (s_WAC_Far)          mono::field_get_value(wac, s_WAC_Far, &bo.far_d);
                    if (s_WAC_CloseFactor)  mono::field_get_value(wac, s_WAC_CloseFactor, &bo.close_f);
                    if (s_WAC_MiddleFactor) mono::field_get_value(wac, s_WAC_MiddleFactor, &bo.middle_f);
                    if (s_WAC_FarFactor)    mono::field_get_value(wac, s_WAC_FarFactor, &bo.far_f);
                    if (s_WAC_ValidRange)   mono::field_get_value(wac, s_WAC_ValidRange, &bo.valid_range);
                    if (s_WAC_PeneRange)    mono::field_get_value(wac, s_WAC_PeneRange, &bo.pene_range);
                    if (s_WAC_Penetrate)    mono::field_get_value(wac, s_WAC_Penetrate, &bo.penetrate);
                    store_boost_orig(wac, bo);
                }

                // Set all distance factors to 100% (full damage at any range)
                float factor_max = 100.0f;
                if (s_WAC_CloseFactor)  mono::field_set_value(wac, s_WAC_CloseFactor, &factor_max);
                if (s_WAC_MiddleFactor) mono::field_set_value(wac, s_WAC_MiddleFactor, &factor_max);
                if (s_WAC_FarFactor)    mono::field_set_value(wac, s_WAC_FarFactor, &factor_max);

                // Extend range thresholds far out
                float huge_range = 9999.0f;
                if (s_WAC_Close)      mono::field_set_value(wac, s_WAC_Close, &huge_range);
                if (s_WAC_Middle)     mono::field_set_value(wac, s_WAC_Middle, &huge_range);
                if (s_WAC_Far)        mono::field_set_value(wac, s_WAC_Far, &huge_range);
                if (s_WAC_ValidRange) mono::field_set_value(wac, s_WAC_ValidRange, &huge_range);

                // Enable penetration and maximize count
                bool pene_on = true;
                if (s_WAC_Penetrate) mono::field_set_value(wac, s_WAC_Penetrate, &pene_on);
                if (s_WAC_PeneRange) mono::field_set_value(wac, s_WAC_PeneRange, &huge_range);
                if (s_WAC_PeneCount) {
                    MonoObject* sp_pc = get_sp_field(wac, s_WAC_PeneCount);
                    if (sp_pc) sp_set_value(sp_pc, 99.0f);
                }

                s_boost_modified = true;
            }

            // --- Weapon ID swap ---
            if (need_idswap && s_WAC_ID) {
                int cur_id = 0;
                mono::field_get_value(wac, s_WAC_ID, &cur_id);
                if (find_id_orig(wac) < 0) store_id_orig(wac, cur_id);
                int target = s_config.weapon_target_id;
                mono::field_set_value(wac, s_WAC_ID, &target);
                s_id_modified = true;
            }

            // --- Read current weapon ID for display (from first WAC) ---
            if (s_WAC_ID && i == 0) {
                // We use the first weapon in the dict as "current"
                // (A more accurate approach would use Calculate.equipInfo)
                mono::field_get_value(wac, s_WAC_ID, &s_current_weapon_id);
            }
        }

        // --- No recoil / no spread at config level ---
        if ((need_recoil || need_spread) && s_PWC_weaponCrosshair) {
            MonoObject* wsc = nullptr;
            mono::field_get_value(pwc, s_PWC_weaponCrosshair, &wsc);
            if (wsc) {
                if (need_recoil) zero_screen_crosshair_recoil(wsc);
                if (need_spread) zero_screen_crosshair_spread(wsc);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// No recoil: SmartCrosshair runtime
// ---------------------------------------------------------------------------
static void apply_no_recoil(MonoObject* pl) {
    if (!s_config.no_recoil) return;
    if (!s_PL_smartCross) return;

    MonoObject* sc = nullptr;
    mono::field_get_value(pl, s_PL_smartCross, &sc);
    if (!sc) return;

    float zero = 0.0f;
    if (s_SC_shakecCamera) mono::field_set_value(sc, s_SC_shakecCamera, &zero);
    if (s_SC_shakeHands)   mono::field_set_value(sc, s_SC_shakeHands, &zero);
    if (s_SC_handsOffset)  mono::field_set_value(sc, s_SC_handsOffset, &zero);
    if (s_SC_cameraUpDown) mono::field_set_value(sc, s_SC_cameraUpDown, &zero);
    if (s_SC_cameraLR)     mono::field_set_value(sc, s_SC_cameraLR, &zero);
}

// ---------------------------------------------------------------------------
// No spread: SmartCrosshair runtime
// ---------------------------------------------------------------------------
static void apply_no_spread(MonoObject* pl) {
    if (!s_config.no_spread) return;
    if (!s_PL_smartCross) return;

    MonoObject* sc = nullptr;
    mono::field_get_value(pl, s_PL_smartCross, &sc);
    if (!sc) return;

    float zero = 0.0f;
    if (s_SC_originPointA) mono::field_set_value(sc, s_SC_originPointA, &zero);
    if (s_SC_curveB)       mono::field_set_value(sc, s_SC_curveB, &zero);
    if (s_SC_overallC)     mono::field_set_value(sc, s_SC_overallC, &zero);
}

// ---------------------------------------------------------------------------
// God mode: keep blood == maxblood each frame
// ---------------------------------------------------------------------------
static void apply_god_mode(MonoObject* pl) {
    if (!s_config.god_mode) return;
    if (!s_PL_player || !s_Player_blood || !s_Player_maxblood) return;

    MonoObject* player = nullptr;
    mono::field_get_value(pl, s_PL_player, &player);
    if (!player) return;

    int maxblood = 0;
    mono::field_get_value(player, s_Player_maxblood, &maxblood);
    if (maxblood > 0) {
        mono::field_set_value(player, s_Player_blood, &maxblood);
    }
}

// ---------------------------------------------------------------------------
// Auto-Fire: call Fire() on WeaponShotScript (works for both PVP and PVE)
// Fire() is the public entry point that handles weapon type dispatch,
// cooldown check, and starts the fire coroutine which calls FireShot().
// In PVE mode, weaponShot is a WeaponShotPVE so polymorphism routes correctly.
// ---------------------------------------------------------------------------
static char s_pve_status_buf[128] = {};

static void apply_auto_fire(MonoObject* pl) {
    if (!s_config.pve_fire_multiply) return;

    // Resolve Fire() by iteration if class_get_method_from_name failed
    if (!s_WSS_Fire && s_WSS_class && mono::class_get_methods && mono::method_get_name) {
        void* iter = nullptr;
        MonoMethod* m = nullptr;
        while ((m = mono::class_get_methods(s_WSS_class, &iter)) != nullptr) {
            const char* n = mono::method_get_name(m);
            if (n && strcmp(n, "Fire") == 0) {
                s_WSS_Fire = m;
                break;
            }
        }
    }

    if (!s_WSS_Fire) {
        snprintf(s_pve_status_buf, sizeof(s_pve_status_buf),
            "ERR:Fire()=null cls=%s", s_WSS_class ? "OK" : "null");
        return;
    }

    // Get weaponShot from PlayerLocal (polymorphic: WSS in PVP, WSP in PVE)
    if (!s_PL_weaponShot) {
        snprintf(s_pve_status_buf, sizeof(s_pve_status_buf), "ERR:no wss field");
        return;
    }

    MonoObject* wss = nullptr;
    mono::field_get_value(pl, s_PL_weaponShot, &wss);
    if (!wss) {
        snprintf(s_pve_status_buf, sizeof(s_pve_status_buf), "ERR:wss=null");
        return;
    }

    int count = s_config.pve_fire_extra;
    if (count < 1) count = 1;
    if (count > 50) count = 50;

    int ok_count = 0, exc_count = 0;
    for (int i = 0; i < count; i++) {
        // Zero _nextFireTime before each Fire() so cooldown doesn't block
        if (s_WSS_nextFireTime) {
            MonoObject* sp_next = get_sp_field(wss, s_WSS_nextFireTime);
            if (sp_next) sp_set_value(sp_next, 0.0f);
        }

        MonoObject* exc = nullptr;
        mono::runtime_invoke(s_WSS_Fire, wss, nullptr, &exc);
        if (exc) exc_count++;
        else ok_count++;
    }

    snprintf(s_pve_status_buf, sizeof(s_pve_status_buf),
        "Fire ok:%d exc:%d x%d", ok_count, exc_count, count);
}

// ---------------------------------------------------------------------------
// Packet sending helpers
// ---------------------------------------------------------------------------
static MonoObject* get_server_net() {
    if (!s_SN_get_Instance) return nullptr;
    return invoke_safe(s_SN_get_Instance);
}

// Create an empty Dictionary<byte, object> by cloning the type from ServerNet.loop.
static MonoObject* create_empty_dict() {
    MonoDomain* domain = mono::get_root_domain();
    if (!domain) return nullptr;

    MonoObject* sn = get_server_net();
    if (!sn || !s_SN_loop) return nullptr;

    MonoObject* loop = nullptr;
    mono::field_get_value(sn, s_SN_loop, &loop);
    if (!loop || !mono::object_get_class) return nullptr;

    MonoClass* dict_class = mono::object_get_class(loop);
    if (!dict_class) return nullptr;

    MonoObject* new_dict = mono::object_new(domain, dict_class);
    if (!new_dict) return nullptr;
    mono::runtime_object_init(new_dict);
    return new_dict;
}

// Fallback: Send a command packet using manual ServerNet.Send call.
// Uses int32 for the enum byte to avoid Mono alignment issues.
static bool send_command_manual(uint8_t cs_code) {
    MonoObject* sn = get_server_net();
    if (!sn || !s_SN_Send_2) return false;

    MonoObject* dict = create_empty_dict();
    if (!dict) return false;

    // Pass enum as int32 to avoid mono runtime_invoke reading garbage from
    // a 1-byte stack variable (Mono may read 4 bytes for value-type params)
    int32_t code32 = static_cast<int32_t>(cs_code);
    void* args[2] = { &code32, dict };
    invoke_safe(s_SN_Send_2, sn, args);
    return true;
}

// Diagnostic: track last PVE action result
static const char* s_last_pve_result = "";

bool send_kill_all_mons() {
    // Preferred: call the game's own static method directly
    if (s_SDT_KillAllMons) {
        MonoObject* ret = invoke_safe(s_SDT_KillAllMons);
        // invoke_safe returns nullptr on exception too, but the method is void
        s_last_pve_result = "KillAll:SDT";
        return true;
    }
    // Fallback: manual send
    bool ok = send_command_manual(CS_KillAllMons);
    s_last_pve_result = ok ? "KillAll:Manual" : "KillAll:FAIL";
    return ok;
}

bool send_damage_all_mons() {
    // No dedicated game method found for CS_AllMonsDamage; use manual send
    bool ok = send_command_manual(CS_AllMonsDamage);
    s_last_pve_result = ok ? "DmgAll:Manual" : "DmgAll:FAIL";
    return ok;
}

// ---------------------------------------------------------------------------
// Channel unlock: spoof level + honor to bypass channel restrictions
// Level check is client-side (100% effective).
// Honor check is server-side (may still be rejected by server).
// ---------------------------------------------------------------------------
static MonoClassField* find_field_utf8(MonoClass* klass, const char* utf8_bytes) {
    if (!klass || !mono::class_get_fields || !mono::field_get_name) return nullptr;
    void* iter = nullptr;
    MonoClassField* f = nullptr;
    while ((f = mono::class_get_fields(klass, &iter)) != nullptr) {
        const char* fn = mono::field_get_name(f);
        if (fn && strcmp(fn, utf8_bytes) == 0) return f;
    }
    return nullptr;
}

static void apply_channel_unlock() {
    if (!s_config.channel_unlock) {
        // Restore originals when disabled
        if (s_orig_level >= 0 && s_Juese_dengji && s_Info_juese && s_DC_get_Player) {
            MonoObject* info = invoke_safe(s_DC_get_Player);
            if (info) {
                MonoObject* juese = nullptr;
                mono::field_get_value(info, s_Info_juese, &juese);
                if (juese) {
                    mono::field_set_value(juese, s_Juese_dengji, &s_orig_level);
                    if (s_Juese_haoyudu && s_orig_honor >= 0)
                        mono::field_set_value(juese, s_Juese_haoyudu, &s_orig_honor);
                }
            }
            s_orig_level = -1;
            s_orig_honor = -1;
        }
        return;
    }

    if (!s_DC_get_Player) return;

    // Get DataCenter.Player (SC.推送.玩家.信息)
    MonoObject* info = invoke_safe(s_DC_get_Player);
    if (!info) return;

    // Lazy-resolve fields from runtime objects (Chinese names)
    if (!s_channel_fields_resolved) {
        MonoClass* info_class = mono::object_get_class(info);
        if (!info_class) return;

        // 角色 field on 信息
        // UTF-8: \xe8\xa7\x92\xe8\x89\xb2
        s_Info_juese = find_field_utf8(info_class, "\xe8\xa7\x92\xe8\x89\xb2");
        if (!s_Info_juese) return;

        MonoObject* juese = nullptr;
        mono::field_get_value(info, s_Info_juese, &juese);
        if (!juese) return;

        MonoClass* juese_class = mono::object_get_class(juese);
        if (!juese_class) return;

        // 等级 field on 角色
        // UTF-8: \xe7\xad\x89\xe7\xba\xa7
        s_Juese_dengji = find_field_utf8(juese_class, "\xe7\xad\x89\xe7\xba\xa7");

        // 好誉度 field on 角色
        // UTF-8: \xe5\xa5\xbd\xe8\xaa\x89\xe5\xba\xa6
        s_Juese_haoyudu = find_field_utf8(juese_class, "\xe5\xa5\xbd\xe8\xaa\x89\xe5\xba\xa6");

        s_channel_fields_resolved = true;
    }

    if (!s_Info_juese || !s_Juese_dengji) return;

    MonoObject* juese = nullptr;
    mono::field_get_value(info, s_Info_juese, &juese);
    if (!juese) return;

    // Save originals on first run
    if (s_orig_level < 0) {
        mono::field_get_value(juese, s_Juese_dengji, &s_orig_level);
        if (s_Juese_haoyudu)
            mono::field_get_value(juese, s_Juese_haoyudu, &s_orig_honor);
    }

    // Set level high enough for any channel (bypasses client-side level check)
    int level = 200;
    mono::field_set_value(juese, s_Juese_dengji, &level);

    // Set honor high (client-side; server may still reject)
    if (s_Juese_haoyudu) {
        int honor = 100;
        mono::field_set_value(juese, s_Juese_haoyudu, &honor);
    }
}

// ---------------------------------------------------------------------------
// Aimbot: aim at nearest enemy head (hold RMB)
// ---------------------------------------------------------------------------
static constexpr float PI = 3.14159265358979323846f;
static constexpr float RAD2DEG = 180.0f / PI;
static constexpr float DEG2RAD = PI / 180.0f;

static float normalize_angle_180(float a) {
    while (a > 180.0f)  a -= 360.0f;
    while (a < -180.0f) a += 360.0f;
    return a;
}

static void apply_aimbot(MonoObject* pl) {
    if (!s_config.aimbot) return;

    // Only aim while right mouse button is held
    if (!gui::get_mouse_button(1)) return;

    // Need all PlayerMouseLook fields
    if (!s_PL_mouseX || !s_PL_mouseY || !s_PML_XRotation || !s_PML_YRotation)
        return;

    // Get mouseX and mouseY PlayerMouseLook objects
    MonoObject* mouseX_obj = nullptr;
    MonoObject* mouseY_obj = nullptr;
    mono::field_get_value(pl, s_PL_mouseX, &mouseX_obj);
    mono::field_get_value(pl, s_PL_mouseY, &mouseY_obj);
    if (!mouseX_obj || !mouseY_obj) return;

    // Read current rotations
    float xRot = 0.0f, yRot = 0.0f;
    mono::field_get_value(mouseX_obj, s_PML_XRotation, &xRot);
    mono::field_get_value(mouseY_obj, s_PML_YRotation, &yRot);

    // Read initial rotation offsets
    unity::Vector2 initRotX = {}, initRotY = {};
    if (s_PML_m_InitialRotation) {
        mono::field_get_value(mouseX_obj, s_PML_m_InitialRotation, &initRotX);
        mono::field_get_value(mouseY_obj, s_PML_m_InitialRotation, &initRotY);
    }

    // Current aim direction in world angles
    float current_yaw   = xRot + initRotX.x;
    float current_pitch = yRot + initRotY.y;

    // Get local camera position
    const auto* local = game_data::get_local_player();
    if (!local) return;
    unity::Vector3 cam_pos = local->head_position;

    // Find closest enemy within FOV cone
    const auto& players = game_data::get_players();
    float best_dist = s_config.aimbot_fov;
    float best_yaw = 0.0f, best_pitch = 0.0f;
    bool found = false;

    for (const auto& p : players) {
        if (p.is_local || p.is_dead) continue;

        // Skip teammates
        if (local->team != game_data::PlayerTeam::NoTeam &&
            p.team == local->team) continue;

        // Direction to enemy head
        float dx = p.head_position.x - cam_pos.x;
        float dy = p.head_position.y - cam_pos.y;
        float dz = p.head_position.z - cam_pos.z;

        float horiz = sqrtf(dx * dx + dz * dz);
        if (horiz < 0.001f) continue;

        float target_yaw   = atan2f(dx, dz) * RAD2DEG;
        float target_pitch = atan2f(dy, horiz) * RAD2DEG;

        float delta_yaw   = normalize_angle_180(target_yaw - current_yaw);
        float delta_pitch = target_pitch - current_pitch;

        float angular_dist = sqrtf(delta_yaw * delta_yaw + delta_pitch * delta_pitch);

        if (angular_dist < best_dist) {
            best_dist  = angular_dist;
            best_yaw   = target_yaw;
            best_pitch = target_pitch;
            found = true;
        }
    }

    if (!found) return;

    // Smooth aim
    float smooth = s_config.aimbot_smooth;
    if (smooth < 1.0f) smooth = 1.0f;
    float factor = 1.0f / smooth;

    float delta_yaw   = normalize_angle_180(best_yaw - current_yaw);
    float delta_pitch = best_pitch - current_pitch;

    float new_yaw   = current_yaw   + delta_yaw   * factor;
    float new_pitch = current_pitch + delta_pitch * factor;

    // Write back (subtract initial rotation offsets)
    float write_x = new_yaw   - initRotX.x;
    float write_y = new_pitch - initRotY.y;

    mono::field_set_value(mouseX_obj, s_PML_XRotation, &write_x);
    mono::field_set_value(mouseY_obj, s_PML_YRotation, &write_y);
}

// ---------------------------------------------------------------------------
// on_update
// ---------------------------------------------------------------------------
void on_update() {
    // Channel unlock runs independently of PlayerLocal
    apply_channel_unlock();

    MonoObject* pl = get_player_local();
    if (!pl) {
        snprintf(s_debug_buf, sizeof(s_debug_buf), "PL=null");
        return;
    }

    apply_aimbot(pl);
    apply_speed_hack(pl);
    apply_rapid_fire_and_runtime(pl);
    apply_fast_knife(pl);
    apply_weapon_mods(pl);
    apply_no_recoil(pl);
    apply_no_spread(pl);
    apply_god_mode(pl);
    apply_auto_fire(pl);  // call Fire() on weaponShot, works for PVP+PVE

    int active = 0;
    if (s_config.aimbot)          active++;
    if (s_config.speed_hack)      active++;
    if (s_config.bullet_speed)    active++;
    if (s_config.infinite_ammo)   active++;
    if (s_config.rapid_fire)      active++;
    if (s_config.fast_knife)      active++;
    if (s_config.no_recoil)       active++;
    if (s_config.no_spread)       active++;
    if (s_config.instant_switch)  active++;
    if (s_config.instant_reload)  active++;
    if (s_config.multi_bullet)    active++;
    if (s_config.range_hack)      active++;
    if (s_config.god_mode)        active++;
    if (s_config.weapon_boost)    active++;
    if (s_config.weapon_id_swap)  active++;

    // Build debug string
    int bc = s_config.infinite_ammo ? get_base_count() : 0;
    if (s_config.pve_fire_multiply) {
        snprintf(s_debug_buf, sizeof(s_debug_buf),
            "Active:%d AutoFire:%s x%d/frm%s%s",
            active,
            s_pve_status_buf,
            s_config.pve_fire_extra,
            s_config.bullet_speed ? " !BulletSpd!" : "",
            s_config.infinite_ammo ? (bc > 0 ? " bc:OK" : " bc:0!") : "");
    } else {
        bool pve_unsafe = s_config.bullet_speed || s_config.range_hack;
        snprintf(s_debug_buf, sizeof(s_debug_buf),
            "Active:%d %s%s%s%s",
            active,
            pve_unsafe ? "!PVE UNSAFE! " : "PVE OK ",
            s_config.bullet_speed ? "[BulletSpd] " : "",
            s_config.range_hack ? "[Range]" : "",
            s_config.infinite_ammo ? (bc > 0 ? " bc:OK" : " bc:0!") : "");
    }
}

// ---------------------------------------------------------------------------
// config / debug / shutdown
// ---------------------------------------------------------------------------
CheatConfig& config() { return s_config; }
const char* get_debug_info() { return s_debug_buf; }
int get_current_weapon_id() { return s_current_weapon_id; }

void shutdown() {
    s_speed_cached      = false;
    s_bs_orig_count     = 0;
    s_bs_modified       = false;
    s_range_orig_count  = 0;
    s_range_modified    = false;
    s_dmg_orig_count    = 0;
    s_dmg_modified      = false;
    s_boost_orig_count  = 0;
    s_boost_modified    = false;
    s_id_orig_count     = 0;
    s_id_modified       = false;
    s_current_weapon_id = 0;
    s_debug_buf[0]      = '\0';

    s_PlayerLocal_class = nullptr;
    s_PL_get_Instance   = nullptr;
    s_PL_motor = nullptr; s_PL_weaponShot = nullptr;
    s_PL_smartCross = nullptr; s_PL_weaponManager = nullptr;
    s_PL_Melee = nullptr;
    s_PL_player = nullptr;

    s_Player_class = nullptr;
    s_Player_blood = nullptr; s_Player_maxblood = nullptr;

    s_ServerNet_class = nullptr;
    s_SN_get_Instance = nullptr; s_SN_Send_2 = nullptr;
    s_SN_Send_3 = nullptr; s_SN_loop = nullptr;

    s_SDT_class = nullptr;
    s_SDT_KillAllMons = nullptr; s_SDT_PVEServerFire = nullptr;
    s_last_pve_result = "";

    s_WSP_class = nullptr; s_WSP_FireShot = nullptr;
    s_WSP_GetInstance = nullptr; s_pve_status_buf[0] = '\0';

    s_MotorYS_class = nullptr; s_Motor_movement = nullptr;
    s_Movement_class = nullptr;
    s_Mov_get_fwd = nullptr; s_Mov_set_fwd = nullptr;
    s_Mov_get_side = nullptr; s_Mov_set_side = nullptr;
    s_Mov_get_back = nullptr; s_Mov_set_back = nullptr;

    s_WSS_class = nullptr; s_WSS_fireRate = nullptr;
    s_WSS_nextFireTime = nullptr; s_WSS_fireTime = nullptr;
    s_WSS_fireCD = nullptr; s_WSS_ShootBullet = nullptr;
    s_WSS_reloadA = nullptr; s_WSS_fire = nullptr;
    s_WSS_Fire = nullptr;

    s_RA_class = nullptr; s_RA_reload = nullptr;
    s_MS_class = nullptr; s_MS_nextShot = nullptr;
    s_GO_GetComponent = nullptr;

    s_SC_class = nullptr;
    s_SC_shakecCamera = nullptr; s_SC_shakeHands = nullptr;
    s_SC_handsOffset = nullptr; s_SC_cameraUpDown = nullptr;
    s_SC_cameraLR = nullptr; s_SC_originPointA = nullptr;
    s_SC_curveB = nullptr; s_SC_overallC = nullptr;

    s_WM_class = nullptr; s_WM_weaponDict = nullptr;

    s_WBI_class = nullptr; s_WBI_bulletCount = nullptr;
    s_WBI_reserveBullet = nullptr; s_WBI_maxBullet = nullptr;
    s_WBI_parseWeaponCfg = nullptr;

    s_PWC_class = nullptr;
    s_PWC_weaponAttribute = nullptr; s_PWC_weaponCrosshair = nullptr;

    s_WAC_class = nullptr; s_WAC_BulletSpeed = nullptr;
    s_WAC_swithTime = nullptr; s_WAC_reloadTime = nullptr;
    s_WAC_shootBullet = nullptr; s_WAC_fireRate = nullptr;
    s_WAC_attactDelay = nullptr; s_WAC_ValidRange = nullptr;
    s_WAC_Reference = nullptr; s_WAC_HightHurt = nullptr;
    s_WAC_ID = nullptr; s_WAC_Close = nullptr;
    s_WAC_Middle = nullptr; s_WAC_Far = nullptr;
    s_WAC_CloseFactor = nullptr; s_WAC_MiddleFactor = nullptr;
    s_WAC_FarFactor = nullptr; s_WAC_Penetrate = nullptr;
    s_WAC_PeneCount = nullptr; s_WAC_PeneRange = nullptr;

    s_WSC_class = nullptr;
    s_WSC_shakecCamera = nullptr; s_WSC_shakeHands = nullptr;
    s_WSC_handsOffset = nullptr; s_WSC_cameraUpDown = nullptr;
    s_WSC_cameraLR = nullptr;
    s_WSC_Standing = nullptr; s_WSC_Moving = nullptr;
    s_WSC_Jumping = nullptr; s_WSC_Crouching = nullptr;

    s_WCS_class = nullptr;
    s_WCS_originPointA = nullptr; s_WCS_curveB = nullptr;
    s_WCS_overallC = nullptr;

    s_SP_class = nullptr;
    s_SP_get_Value = nullptr; s_SP_SetValue = nullptr;

    s_GM_class = nullptr; s_GM_baseCount = nullptr;

    s_PML_class = nullptr;
    s_PML_XRotation = nullptr; s_PML_YRotation = nullptr;
    s_PML_m_InitialRotation = nullptr;
    s_PL_mouseX = nullptr; s_PL_mouseY = nullptr;

    s_DC_class = nullptr; s_DC_get_Player = nullptr;
    s_Info_juese = nullptr; s_Juese_dengji = nullptr;
    s_Juese_haoyudu = nullptr;
    s_channel_fields_resolved = false;
    s_orig_level = -1; s_orig_honor = -1;
}

} // namespace cheats
