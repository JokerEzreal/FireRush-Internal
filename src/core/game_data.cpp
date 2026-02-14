#include "game_data.h"
#include "../mono/mono_api.h"
#include "../mono/mono_types.h"
#include "../unity/unity_classes.h"

#include <cstring>
#include <cstdio>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <Windows.h>

namespace game_data {

// ---------------------------------------------------------------------------
// Mono invoke helper (same pattern as gui.cpp)
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

// Classes
static MonoClass* s_PlayerLocal_class   = nullptr;
static MonoClass* s_GameManager_class   = nullptr;
static MonoClass* s_Player_class        = nullptr;
static MonoClass* s_Transform_class     = nullptr;
static MonoClass* s_GameObject_class    = nullptr;

// PlayerLocal methods / fields
static MonoMethod*     s_PL_get_Instance   = nullptr;
static MonoClassField* s_PL_player_field   = nullptr;  // PlayerLocal.player -> Player

// GameManager methods
static MonoMethod* s_GM_get_Instance    = nullptr;
static MonoMethod* s_GM_get_Players     = nullptr;

// Dictionary<PlayerSign, Player> iteration
static MonoMethod* s_Dict_get_Count     = nullptr;
static MonoMethod* s_Dict_get_Values    = nullptr;
static MonoMethod* s_ValueColl_GetEnumerator = nullptr;
static MonoMethod* s_Enumerator_MoveNext     = nullptr;
static MonoMethod* s_Enumerator_get_Current  = nullptr;
static MonoMethod* s_Enumerator_Dispose      = nullptr;

// Player fields
static MonoClassField* s_Player_PlayerID        = nullptr;  // PlayerSign struct
static MonoClassField* s_Player_PlayerName      = nullptr;  // string
static MonoClassField* s_Player_PlayerTransform = nullptr;  // Transform
static MonoClassField* s_Player_blood           = nullptr;  // int
static MonoClassField* s_Player_maxblood        = nullptr;  // int
static MonoClassField* s_Player_isDead          = nullptr;  // bool
static MonoClassField* s_Player_localPlayer     = nullptr;  // bool
static MonoClassField* s_Player_playerTeam      = nullptr;  // PlayerTeamEnum (byte)
static MonoClassField* s_Player_eye             = nullptr;  // GameObject
static MonoClassField* s_Player_battlePlayer    = nullptr;  // BattlePlayerInfoEnum
static MonoClassField* s_Player_showName        = nullptr;  // ShowPlayerName

// BattlePlayerInfoEnum fields (actual HP source)
static MonoClass*      s_BattlePlayerInfo_class = nullptr;
static MonoClassField* s_BPI_Hp                 = nullptr;  // int
static MonoClassField* s_BPI_MaxHP              = nullptr;  // int
static MonoClassField* s_BPI_pos                = nullptr;  // Vector3
static MonoClassField* s_BPI_isAlive            = nullptr;  // bool
static MonoClassField* s_BPI_PlayerTeam         = nullptr;  // PlayerTeamEnum
static MonoMethod*     s_BPI_get_singleton       = nullptr;  // static singleton getter method

// ShowPlayerName fields (actual name source)
static MonoClass*      s_ShowPlayerName_class   = nullptr;
static MonoClassField* s_SPN_ShowName           = nullptr;  // string (private)

// Player.playerRemote -> PlayerRemote (for remote player data)
static MonoClassField* s_Player_playerRemote    = nullptr;

// DataCenter (static fields for player name)
static MonoClass*      s_DataCenter_class       = nullptr;
static MonoClassField* s_DC_rPlayer             = nullptr;  // static SC.推送.玩家.信息
static MonoClassField* s_DC_CurrentPlayerSign   = nullptr;  // static PlayerSign

// SC.推送.玩家.信息 -> 角色 -> 昵称
static MonoClass*      s_PlayerInfo_class       = nullptr;  // SC.推送.玩家.信息
static MonoClassField* s_PI_角色                = nullptr;  // 角色 field
static MonoClass*      s_角色_class             = nullptr;  // SC.推送.玩家.角色
static MonoClassField* s_角色_昵称              = nullptr;  // 昵称 (nickname) string

// ServerDataTrans -> roomCommandPlayerDict -> RoomCommandPlayer.Nick (all player names)
static MonoClass*      s_ServerDataTrans_class   = nullptr;
static MonoClassField* s_SDT_roomCmdPlayerDict   = nullptr;  // static Dict<PlayerSign, RoomCommandPlayer>
static MonoClass*      s_RoomCmdPlayer_class     = nullptr;
static MonoClassField* s_RCP_Nick                = nullptr;  // string (offset 0x18)
static MonoClassField* s_RCP_TargetPlayerID      = nullptr;  // PlayerSign struct (offset 0x08)

// NGUI_BattleManager -> helth -> HP
static MonoClass*      s_NGUI_BM_class          = nullptr;
static MonoMethod*     s_NGUI_BM_get_Instance   = nullptr;
static MonoClassField* s_NGUI_BM_helth          = nullptr;  // NGUI_Helth
static MonoClass*      s_NGUI_Helth_class       = nullptr;
static MonoClassField* s_Helth_currentHP        = nullptr;  // int
static MonoClassField* s_Helth_maxHP            = nullptr;  // int
static MonoClassField* s_Helth_hpSlider         = nullptr;  // UIProgressBar/UISlider
static MonoClassField* s_Helth_hpLabel          = nullptr;  // UILabel

// NGUI UI component fields (resolved lazily from runtime objects)
static MonoClassField* s_UISlider_mValue        = nullptr;  // float (0-1)
static MonoClassField* s_UILabel_mText          = nullptr;  // string
static bool s_ui_fields_resolved                = false;

// PlayerLocal.mainCamera for real camera position
static MonoClassField* s_PL_mainCamera          = nullptr;  // GameObject

// FindObjectsOfType approach for remote players
static MonoClass*  s_UE_Object_class            = nullptr;  // UnityEngine.Object
static MonoClass*  s_Component_class            = nullptr;  // UnityEngine.Component
static MonoMethod* s_FindObjectsOfType          = nullptr;  // Object.FindObjectsOfType(Type)
static MonoMethod* s_Component_get_transform    = nullptr;  // Component.get_transform()

// ShowPlayerName fields for FOT path
static MonoClassField* s_SPN_PlayerID           = nullptr;  // PlayerSign struct
static MonoClassField* s_SPN_isDead             = nullptr;  // bool
static MonoClassField* s_SPN_target             = nullptr;  // Transform (nameplate above head)
static MonoClassField* s_SPN_playerLayer        = nullptr;  // LayerEnum (Int32): Partner=5, Enemy=6
static MonoClassField* s_SPN_texClone           = nullptr;  // string (icon/display name)

// Detailed probe string (populated during dump)
static std::string s_probe_info;

// Transform.get_position
static MonoMethod* s_Transform_get_position = nullptr;

// GameObject.get_transform
static MonoMethod* s_GO_get_transform = nullptr;

// ---------------------------------------------------------------------------
// Player data storage
// ---------------------------------------------------------------------------
static std::vector<GamePlayerData> s_players;
static int s_local_index = -1;

// ---------------------------------------------------------------------------
// Debug info string, built during each update() call
// ---------------------------------------------------------------------------
static std::string s_debug_info;

// ---------------------------------------------------------------------------
// HP diagnostic logging
// ---------------------------------------------------------------------------
static HPDiagnostic s_local_hp_diag;
static bool  s_hp_logging       = false;
static int   s_hp_log_counter   = 0;
static int   s_hp_log_line      = 0;
static std::string s_hp_log_path;

// ---------------------------------------------------------------------------
// initialize
// ---------------------------------------------------------------------------
bool initialize() {
    MonoImage* csharp_img = unity::images().assembly_csharp;
    MonoImage* engine_img = unity::images().unity_engine;

    if (!csharp_img || !engine_img) return false;

    // -- Resolve classes --
    s_PlayerLocal_class = mono::class_from_name(csharp_img, "", "PlayerLocal");
    s_GameManager_class = mono::class_from_name(csharp_img, "", "GameManager");
    s_Player_class      = mono::class_from_name(csharp_img, "", "Player");
    s_Transform_class   = mono::class_from_name(engine_img, "UnityEngine", "Transform");
    s_GameObject_class  = mono::class_from_name(engine_img, "UnityEngine", "GameObject");

    if (!s_Player_class || !s_Transform_class)
        return false;

    // -- PlayerLocal methods/fields --
    if (s_PlayerLocal_class) {
        s_PL_get_Instance = mono::class_get_method_from_name(s_PlayerLocal_class, "get_Instance", 0);
        s_PL_player_field = mono::class_get_field_from_name(s_PlayerLocal_class, "player");
    }

    // -- GameManager methods --
    if (s_GameManager_class) {
        s_GM_get_Instance = mono::class_get_method_from_name(s_GameManager_class, "get_Instance", 0);
        s_GM_get_Players  = mono::class_get_method_from_name(s_GameManager_class, "get_Players", 0);
    }

    // -- Player fields --
    s_Player_PlayerID        = mono::class_get_field_from_name(s_Player_class, "PlayerID");
    s_Player_PlayerName      = mono::class_get_field_from_name(s_Player_class, "PlayerName");
    s_Player_PlayerTransform = mono::class_get_field_from_name(s_Player_class, "PlayerTransform");
    s_Player_blood           = mono::class_get_field_from_name(s_Player_class, "blood");
    s_Player_maxblood        = mono::class_get_field_from_name(s_Player_class, "maxblood");
    s_Player_isDead          = mono::class_get_field_from_name(s_Player_class, "_isDead");
    s_Player_localPlayer     = mono::class_get_field_from_name(s_Player_class, "localPlayer");
    s_Player_playerTeam      = mono::class_get_field_from_name(s_Player_class, "_playerTeam");
    s_Player_eye             = mono::class_get_field_from_name(s_Player_class, "eye");
    s_Player_battlePlayer    = mono::class_get_field_from_name(s_Player_class, "battlePlayer");
    s_Player_showName        = mono::class_get_field_from_name(s_Player_class, "showName");

    s_Player_playerRemote    = mono::class_get_field_from_name(s_Player_class, "playerRemote");

    // -- BattlePlayerInfoEnum (actual HP source) --
    s_BattlePlayerInfo_class = mono::class_from_name(csharp_img, "", "BattlePlayerInfoEnum");
    if (s_BattlePlayerInfo_class) {
        s_BPI_Hp        = mono::class_get_field_from_name(s_BattlePlayerInfo_class, "Hp");
        s_BPI_MaxHP     = mono::class_get_field_from_name(s_BattlePlayerInfo_class, "MaxHP");
        s_BPI_pos       = mono::class_get_field_from_name(s_BattlePlayerInfo_class, "pos");
        s_BPI_isAlive   = mono::class_get_field_from_name(s_BattlePlayerInfo_class, "isAlive");
        s_BPI_PlayerTeam= mono::class_get_field_from_name(s_BattlePlayerInfo_class, "PlayerTeam");
        // Static singleton getter method (obfuscated name)
        s_BPI_get_singleton = mono::class_get_method_from_name(s_BattlePlayerInfo_class, "uoBIPsifzaSlBUJ5cPPs", 0);
    }

    // -- ShowPlayerName (actual name source) --
    s_ShowPlayerName_class = mono::class_from_name(csharp_img, "", "ShowPlayerName");
    if (s_ShowPlayerName_class) {
        s_SPN_ShowName = mono::class_get_field_from_name(s_ShowPlayerName_class, "ShowName");
    }

    // -- Transform.get_position --
    s_Transform_get_position = mono::class_get_method_from_name(s_Transform_class, "get_position", 0);

    // -- GameObject.get_transform --
    if (s_GameObject_class) {
        s_GO_get_transform = mono::class_get_method_from_name(s_GameObject_class, "get_transform", 0);
    }

    // -- PlayerLocal.mainCamera --
    if (s_PlayerLocal_class) {
        s_PL_mainCamera = mono::class_get_field_from_name(s_PlayerLocal_class, "mainCamera");
    }

    // -- FindObjectsOfType approach --
    s_UE_Object_class = mono::class_from_name(engine_img, "UnityEngine", "Object");
    s_Component_class = mono::class_from_name(engine_img, "UnityEngine", "Component");
    if (s_UE_Object_class) {
        s_FindObjectsOfType = mono::class_get_method_from_name(s_UE_Object_class, "FindObjectsOfType", 1);
    }
    if (s_Component_class) {
        s_Component_get_transform = mono::class_get_method_from_name(s_Component_class, "get_transform", 0);
    }

    // -- ShowPlayerName fields for FOT path --
    if (s_ShowPlayerName_class) {
        s_SPN_PlayerID    = mono::class_get_field_from_name(s_ShowPlayerName_class, "PlayerID");
        s_SPN_isDead      = mono::class_get_field_from_name(s_ShowPlayerName_class, "isDead");
        s_SPN_target      = mono::class_get_field_from_name(s_ShowPlayerName_class, "target");
        s_SPN_playerLayer = mono::class_get_field_from_name(s_ShowPlayerName_class, "playerLayer");
        s_SPN_texClone    = mono::class_get_field_from_name(s_ShowPlayerName_class, "texClone");
    }

    // -- ServerDataTrans / RoomCommandPlayer (player names for all players) --
    s_ServerDataTrans_class = mono::class_from_name(csharp_img, "", "ServerDataTrans");
    if (s_ServerDataTrans_class) {
        s_SDT_roomCmdPlayerDict = mono::class_get_field_from_name(s_ServerDataTrans_class, "roomCommandPlayerDict");
    }
    s_RoomCmdPlayer_class = mono::class_from_name(csharp_img, "", "RoomCommandPlayer");
    if (s_RoomCmdPlayer_class) {
        s_RCP_Nick            = mono::class_get_field_from_name(s_RoomCmdPlayer_class, "Nick");
        s_RCP_TargetPlayerID  = mono::class_get_field_from_name(s_RoomCmdPlayer_class, "TargetPlayerID");
    }

    // -- DataCenter (static fields, needs DataLib image) --
    s_DataCenter_class = mono::class_from_name(csharp_img, "", "DataCenter");
    if (s_DataCenter_class) {
        s_DC_rPlayer           = mono::class_get_field_from_name(s_DataCenter_class, "rPlayer");
        s_DC_CurrentPlayerSign = mono::class_get_field_from_name(s_DataCenter_class, "CurrentPlayerSign");
    }

    // -- SC.推送.玩家.信息 and 角色 (in DataLib) --
    MonoImage* datalib_img = nullptr;
    // Find DataLib image
    struct FindCtx { const char* name; MonoImage* img; };
    FindCtx ctx = {"DataLib", nullptr};
    mono::assembly_foreach([](MonoAssembly* asm_, void* ud) {
        auto* c = static_cast<FindCtx*>(ud);
        MonoImage* img = mono::assembly_get_image(asm_);
        if (img) {
            const char* n = mono::image_get_name(img);
            if (n && strcmp(n, c->name) == 0) c->img = img;
        }
    }, &ctx);
    datalib_img = ctx.img;

    if (datalib_img) {
        // SC.推送.玩家.信息
        s_PlayerInfo_class = mono::class_from_name(datalib_img, "SC.\xE6\x8E\xA8\xE9\x80\x81.\xE7\x8E\xA9\xE5\xAE\xB6", "\xE4\xBF\xA1\xE6\x81\xAF");
        if (s_PlayerInfo_class) {
            s_PI_角色 = mono::class_get_field_from_name(s_PlayerInfo_class, "\xE8\xA7\x92\xE8\x89\xB2");
        }
        // SC.推送.玩家.角色
        s_角色_class = mono::class_from_name(datalib_img, "SC.\xE6\x8E\xA8\xE9\x80\x81.\xE7\x8E\xA9\xE5\xAE\xB6", "\xE8\xA7\x92\xE8\x89\xB2");
        if (s_角色_class) {
            s_角色_昵称 = mono::class_get_field_from_name(s_角色_class, "\xE6\x98\xB5\xE7\xA7\xB0");
        }
    }

    // -- NGUI_BattleManager -> helth -> HP --
    s_NGUI_BM_class = mono::class_from_name(csharp_img, "", "NGUI_BattleManager");
    if (s_NGUI_BM_class) {
        s_NGUI_BM_get_Instance = mono::class_get_method_from_name(s_NGUI_BM_class, "get_Instance", 0);
        s_NGUI_BM_helth = mono::class_get_field_from_name(s_NGUI_BM_class, "helth");
    }
    s_NGUI_Helth_class = mono::class_from_name(csharp_img, "", "NGUI_Helth");
    if (s_NGUI_Helth_class) {
        s_Helth_currentHP = mono::class_get_field_from_name(s_NGUI_Helth_class, "currentHP");
        s_Helth_maxHP     = mono::class_get_field_from_name(s_NGUI_Helth_class, "maxHP");
        s_Helth_hpSlider  = mono::class_get_field_from_name(s_NGUI_Helth_class, "hpSlider");
        s_Helth_hpLabel   = mono::class_get_field_from_name(s_NGUI_Helth_class, "hpLabel");
    }

    // -- Dictionary iteration methods are resolved lazily on first update --

    return true;
}

// ---------------------------------------------------------------------------
// Helper: read Vector3 position from a Transform MonoObject*
// ---------------------------------------------------------------------------
static unity::Vector3 read_transform_position(MonoObject* transform_obj) {
    unity::Vector3 pos{};
    if (!transform_obj || !s_Transform_get_position) return pos;

    MonoObject* result = invoke_safe(s_Transform_get_position, transform_obj);
    if (result) {
        pos = *static_cast<unity::Vector3*>(mono::object_unbox(result));
    }
    return pos;
}

// ---------------------------------------------------------------------------
// Helper: resolve Dictionary iteration methods from a dictionary instance
// ---------------------------------------------------------------------------
static bool resolve_dict_methods(MonoObject* dict_obj) {
    if (s_Dict_get_Count) return true;  // already resolved
    if (!dict_obj) return false;

    MonoClass* dict_class = mono::object_get_class(dict_obj);
    if (!dict_class) return false;

    s_Dict_get_Count  = mono::class_get_method_from_name(dict_class, "get_Count", 0);
    s_Dict_get_Values = mono::class_get_method_from_name(dict_class, "get_Values", 0);

    if (!s_Dict_get_Values) return false;

    // Call get_Values() to get the ValueCollection, then resolve its methods
    MonoObject* values_coll = invoke_safe(s_Dict_get_Values, dict_obj);
    if (!values_coll) return false;

    MonoClass* values_class = mono::object_get_class(values_coll);
    if (!values_class) return false;

    s_ValueColl_GetEnumerator = mono::class_get_method_from_name(values_class, "GetEnumerator", 0);

    if (!s_ValueColl_GetEnumerator) return false;

    // We need to get an enumerator instance to resolve its class methods
    MonoObject* enumerator = invoke_safe(s_ValueColl_GetEnumerator, values_coll);
    if (!enumerator) return false;

    MonoClass* enum_class = mono::object_get_class(enumerator);
    if (!enum_class) return false;

    s_Enumerator_MoveNext    = mono::class_get_method_from_name(enum_class, "MoveNext", 0);
    s_Enumerator_get_Current = mono::class_get_method_from_name(enum_class, "get_Current", 0);
    s_Enumerator_Dispose     = mono::class_get_method_from_name(enum_class, "Dispose", 0);

    return s_Enumerator_MoveNext && s_Enumerator_get_Current;
}

// ---------------------------------------------------------------------------
// Helper: read a single Player object into GamePlayerData
// ---------------------------------------------------------------------------
static bool read_player(MonoObject* player_obj, GamePlayerData& out) {
    if (!player_obj) return false;

    // -- PlayerID (PlayerSign is a value type embedded in the Player object) --
    if (s_Player_PlayerID) {
        uint8_t ps_buf[32] = {};
        mono::field_get_value(player_obj, s_Player_PlayerID, ps_buf);
        out.player_id = *reinterpret_cast<int*>(ps_buf + 0);
        out.server_id = *reinterpret_cast<uint8_t*>(ps_buf + 4);
    }

    // -- Name: prefer ShowPlayerName.ShowName, fallback to Player.PlayerName --
    if (s_Player_showName && s_SPN_ShowName) {
        MonoObject* show_name_obj = nullptr;
        mono::field_get_value(player_obj, s_Player_showName, &show_name_obj);
        if (show_name_obj) {
            MonoString* name_str = nullptr;
            mono::field_get_value(show_name_obj, s_SPN_ShowName, &name_str);
            if (name_str) {
                char* utf8 = mono::string_to_utf8(name_str);
                if (utf8) {
                    out.name = utf8;
                    if (mono::free_mem) mono::free_mem(utf8);
                }
            }
        }
    }
    // Fallback 2: Player.PlayerName
    if (out.name.empty() && s_Player_PlayerName) {
        MonoString* name_str = nullptr;
        mono::field_get_value(player_obj, s_Player_PlayerName, &name_str);
        if (name_str) {
            char* utf8 = mono::string_to_utf8(name_str);
            if (utf8) {
                out.name = utf8;
                if (mono::free_mem) mono::free_mem(utf8);
            }
        }
    }
    // Fallback 3: DataCenter.rPlayer -> 角色 -> 昵称 (static field, LOCAL player only)
    if (out.name.empty() && out.is_local && s_DC_rPlayer && s_PI_角色 && s_角色_昵称 &&
        mono::class_vtable && mono::field_static_get_value) {
        MonoDomain* domain = mono::get_root_domain();
        MonoVTable* dc_vt = domain ? mono::class_vtable(domain, s_DataCenter_class) : nullptr;
        if (dc_vt) {
            MonoObject* rPlayer = nullptr;
            mono::field_static_get_value(dc_vt, s_DC_rPlayer, &rPlayer);
            if (rPlayer) {
                MonoObject* 角色_obj = nullptr;
                mono::field_get_value(rPlayer, s_PI_角色, &角色_obj);
                if (角色_obj) {
                    MonoString* nick = nullptr;
                    mono::field_get_value(角色_obj, s_角色_昵称, &nick);
                    if (nick) {
                        char* utf8 = mono::string_to_utf8(nick);
                        if (utf8) {
                            out.name = utf8;
                            if (mono::free_mem) mono::free_mem(utf8);
                        }
                    }
                }
            }
        }
    }

    // -- HP: read ALL sources unconditionally for diagnostics --
    HPDiagnostic& diag = out.hp_diag;

    // Source A: Player.battlePlayer -> BattlePlayerInfoEnum.Hp/MaxHP
    if (s_Player_battlePlayer && s_BPI_Hp && s_BPI_MaxHP) {
        MonoObject* bpi_obj = nullptr;
        mono::field_get_value(player_obj, s_Player_battlePlayer, &bpi_obj);
        if (bpi_obj) {
            diag.src_a_valid = true;
            mono::field_get_value(bpi_obj, s_BPI_Hp,    &diag.src_a_hp);
            mono::field_get_value(bpi_obj, s_BPI_MaxHP, &diag.src_a_maxhp);
        }
    }

    // Source B: BattlePlayerInfoEnum static singleton
    if (s_BPI_get_singleton && s_BPI_Hp && s_BPI_MaxHP) {
        MonoObject* bpi_single = invoke_safe(s_BPI_get_singleton);
        if (bpi_single) {
            diag.src_b_valid = true;
            mono::field_get_value(bpi_single, s_BPI_Hp,    &diag.src_b_hp);
            mono::field_get_value(bpi_single, s_BPI_MaxHP, &diag.src_b_maxhp);
        }
    }

    // Source C + E + F: all from NGUI_BattleManager.Instance.helth
    MonoObject* helth_obj = nullptr;
    if (s_NGUI_BM_get_Instance && s_NGUI_BM_helth) {
        MonoObject* bm_inst = invoke_safe(s_NGUI_BM_get_Instance);
        if (bm_inst) {
            mono::field_get_value(bm_inst, s_NGUI_BM_helth, &helth_obj);
        }
    }

    // Source C: NGUI_Helth.currentHP/maxHP
    if (helth_obj && s_Helth_currentHP && s_Helth_maxHP) {
        diag.src_c_valid = true;
        mono::field_get_value(helth_obj, s_Helth_currentHP, &diag.src_c_hp);
        mono::field_get_value(helth_obj, s_Helth_maxHP,     &diag.src_c_maxhp);
    }

    // Source E: NGUI_Helth.hpSlider -> UIProgressBar/UISlider.mValue (float 0-1)
    if (helth_obj && s_Helth_hpSlider) {
        MonoObject* slider_obj = nullptr;
        mono::field_get_value(helth_obj, s_Helth_hpSlider, &slider_obj);
        if (slider_obj) {
            // Lazy resolve UISlider/UIProgressBar.mValue field from runtime object
            if (!s_UISlider_mValue) {
                MonoClass* slider_class = mono::object_get_class(slider_obj);
                if (slider_class) {
                    s_UISlider_mValue = mono::class_get_field_from_name(slider_class, "mValue");
                }
            }
            if (s_UISlider_mValue) {
                float val = -1.0f;
                mono::field_get_value(slider_obj, s_UISlider_mValue, &val);
                diag.src_e_slider = val;
                diag.src_e_valid = true;
            }
        }
    }

    // Source F: NGUI_Helth.hpLabel -> UILabel.mText (string displayed on HUD)
    if (helth_obj && s_Helth_hpLabel) {
        MonoObject* label_obj = nullptr;
        mono::field_get_value(helth_obj, s_Helth_hpLabel, &label_obj);
        if (label_obj) {
            // Lazy resolve UILabel.mText field from runtime object
            if (!s_UILabel_mText) {
                MonoClass* label_class = mono::object_get_class(label_obj);
                if (label_class) {
                    s_UILabel_mText = mono::class_get_field_from_name(label_class, "mText");
                }
                s_ui_fields_resolved = true;
            }
            if (s_UILabel_mText) {
                MonoString* text = nullptr;
                mono::field_get_value(label_obj, s_UILabel_mText, &text);
                if (text) {
                    char* utf8 = mono::string_to_utf8(text);
                    if (utf8) {
                        strncpy(diag.src_f_label, utf8, sizeof(diag.src_f_label) - 1);
                        diag.src_f_label[sizeof(diag.src_f_label) - 1] = '\0';
                        if (mono::free_mem) mono::free_mem(utf8);
                    }
                    diag.src_f_valid = true;
                }
            }
        }
    }

    // Source D: Player.blood / maxblood (legacy)
    if (s_Player_blood)
        mono::field_get_value(player_obj, s_Player_blood, &diag.src_d_hp);
    if (s_Player_maxblood)
        mono::field_get_value(player_obj, s_Player_maxblood, &diag.src_d_maxhp);

    // Pick the best source for blood/max_blood (first non-zero wins)
    if (diag.src_a_valid && (diag.src_a_hp != 0 || diag.src_a_maxhp != 0)) {
        out.blood     = diag.src_a_hp;
        out.max_blood = diag.src_a_maxhp;
        diag.used_source = 'A';
    } else if (diag.src_b_valid && (diag.src_b_hp != 0 || diag.src_b_maxhp != 0)) {
        out.blood     = diag.src_b_hp;
        out.max_blood = diag.src_b_maxhp;
        diag.used_source = 'B';
    } else if (diag.src_c_valid && (diag.src_c_hp != 0 || diag.src_c_maxhp != 0)) {
        out.blood     = diag.src_c_hp;
        out.max_blood = diag.src_c_maxhp;
        diag.used_source = 'C';
    } else if (diag.src_d_hp != 0 || diag.src_d_maxhp != 0) {
        out.blood     = diag.src_d_hp;
        out.max_blood = diag.src_d_maxhp;
        diag.used_source = 'D';
    }

    // -- _isDead --
    if (s_Player_isDead) {
        mono_bool dead = 0;
        mono::field_get_value(player_obj, s_Player_isDead, &dead);
        out.is_dead = (dead != 0);
    }

    // -- localPlayer --
    if (s_Player_localPlayer) {
        mono_bool local = 0;
        mono::field_get_value(player_obj, s_Player_localPlayer, &local);
        out.is_local = (local != 0);
    }

    // -- _playerTeam (byte-backed enum) --
    if (s_Player_playerTeam) {
        uint8_t team_val = 0;
        mono::field_get_value(player_obj, s_Player_playerTeam, &team_val);
        out.team = static_cast<PlayerTeam>(team_val);
    }

    // -- PlayerTransform -> world position --
    if (s_Player_PlayerTransform) {
        MonoObject* transform_obj = nullptr;
        mono::field_get_value(player_obj, s_Player_PlayerTransform, &transform_obj);
        out.world_position = read_transform_position(transform_obj);
    }

    // -- eye (GameObject) -> head position --
    if (s_Player_eye && s_GO_get_transform) {
        MonoObject* eye_go = nullptr;
        mono::field_get_value(player_obj, s_Player_eye, &eye_go);
        if (eye_go) {
            MonoObject* eye_transform = invoke_safe(s_GO_get_transform, eye_go);
            out.head_position = read_transform_position(eye_transform);
        }
    }
    // Fallback head position
    if (out.head_position.x == 0 && out.head_position.y == 0 && out.head_position.z == 0) {
        if (out.is_local && s_PL_get_Instance && s_PL_mainCamera && s_GO_get_transform) {
            // Local player only: use camera position as head
            MonoObject* pl_inst = invoke_safe(s_PL_get_Instance);
            if (pl_inst) {
                MonoObject* cam_go = nullptr;
                mono::field_get_value(pl_inst, s_PL_mainCamera, &cam_go);
                if (cam_go) {
                    MonoObject* cam_tf = invoke_safe(s_GO_get_transform, cam_go);
                    out.head_position = read_transform_position(cam_tf);
                }
            }
        }
        // Remote players (or local if camera fallback failed): estimate head from feet + height
        if (out.head_position.x == 0 && out.head_position.y == 0 && out.head_position.z == 0) {
            out.head_position = out.world_position;
            out.head_position.y += 1.8f;  // approximate player height
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Helper: try PlayerLocal path to get local player directly
// Returns the Player MonoObject* from PlayerLocal.Instance.player, or nullptr
// ---------------------------------------------------------------------------
static MonoObject* try_player_local_path(std::string& dbg) {
    if (!s_PL_get_Instance) {
        dbg += "PL=no_method ";
        return nullptr;
    }

    MonoObject* pl_instance = invoke_safe(s_PL_get_Instance);
    if (!pl_instance) {
        dbg += "PL=null ";
        return nullptr;
    }
    dbg += "PL=OK ";

    if (!s_PL_player_field) {
        dbg += "plr_field=missing ";
        return nullptr;
    }

    MonoObject* player_obj = nullptr;
    mono::field_get_value(pl_instance, s_PL_player_field, &player_obj);
    if (!player_obj) {
        dbg += "plr=null ";
        return nullptr;
    }
    dbg += "plr=OK ";

    return player_obj;
}

// ---------------------------------------------------------------------------
// Mono Dictionary<K,V> internal layout (old Mono runtime):
//   offset 0x08: table        (int[])
//   offset 0x0C: linkSlots    (Link[])  -- each Link is {int hashCode, int next} = 8 bytes
//   offset 0x10: keySlots     (TKey[])
//   offset 0x14: valueSlots   (TValue[])
//   offset 0x18: touchedSlots (int)
//   offset 0x20: count        (int)
// Valid entry: (linkSlots[i].hashCode & 0x80000000) != 0
// ---------------------------------------------------------------------------
static constexpr int DICT_OFF_LINK_SLOTS    = 0x0C;
static constexpr int DICT_OFF_VALUE_SLOTS   = 0x14;
static constexpr int DICT_OFF_TOUCHED_SLOTS = 0x18;
static constexpr int DICT_OFF_COUNT         = 0x20;
static constexpr int MONO_ARRAY_DATA        = 0x10;  // array element data offset (x86)
static constexpr int LINK_STRUCT_SIZE       = 8;     // sizeof(Link) = {hashCode, next}
static constexpr int HASH_FLAG              = (int)0x80000000;

// ---------------------------------------------------------------------------
// Helper: GameManager dictionary path to get all players
// Reads the dictionary's internal valueSlots[] array directly from memory,
// bypassing mono_runtime_invoke for iteration (which has threading issues).
// ---------------------------------------------------------------------------
static bool try_game_manager_path(std::string& dbg) {
    if (!s_GM_get_Instance) {
        dbg += "GM=no_method ";
        return false;
    }

    MonoObject* gm_instance = invoke_safe(s_GM_get_Instance);
    if (!gm_instance) {
        dbg += "GM=null ";
        return false;
    }
    dbg += "GM=OK ";

    if (!s_GM_get_Players) {
        dbg += "Players=no_method ";
        return false;
    }

    MonoObject* players_dict = invoke_safe(s_GM_get_Players, gm_instance);
    if (!players_dict) {
        dbg += "dict=null ";
        return false;
    }
    dbg += "dict=OK ";

    // Read dictionary internals directly from memory
    char* dict_ptr = reinterpret_cast<char*>(players_dict);

    int count   = *reinterpret_cast<int*>(dict_ptr + DICT_OFF_COUNT);
    int touched = *reinterpret_cast<int*>(dict_ptr + DICT_OFF_TOUCHED_SLOTS);

    dbg += "count=" + std::to_string(count) + " ";
    dbg += "touched=" + std::to_string(touched) + " ";

    if (count <= 0 || touched <= 0) {
        return false;
    }

    char* link_arr  = *reinterpret_cast<char**>(dict_ptr + DICT_OFF_LINK_SLOTS);
    char* value_arr = *reinterpret_cast<char**>(dict_ptr + DICT_OFF_VALUE_SLOTS);

    if (!link_arr || !value_arr) {
        dbg += "arrays=null ";
        return false;
    }

    char* link_data  = link_arr  + MONO_ARRAY_DATA;
    char* value_data = value_arr + MONO_ARRAY_DATA;

    s_players.reserve(count);

    for (int i = 0; i < touched && i < 64; ++i) {
        // Check validity: linkSlots[i].hashCode & HASH_FLAG
        int hash_code = *reinterpret_cast<int*>(link_data + i * LINK_STRUCT_SIZE);
        if ((hash_code & HASH_FLAG) == 0) continue;

        // Read Player object pointer from valueSlots[i]
        MonoObject* player_obj = *reinterpret_cast<MonoObject**>(value_data + i * sizeof(void*));
        if (!player_obj) continue;

        GamePlayerData data{};
        if (read_player(player_obj, data)) {
            if (data.is_local) {
                s_local_index = static_cast<int>(s_players.size());
            }
            s_players.push_back(std::move(data));
        }
    }

    return !s_players.empty();
}

// ---------------------------------------------------------------------------
// Helper: FindObjectsOfType(ShowPlayerName) to get remote players
// ---------------------------------------------------------------------------
static bool try_find_objects_path(std::string& dbg, PlayerTeam local_team) {
    if (!s_FindObjectsOfType || !s_ShowPlayerName_class ||
        !mono::class_get_type || !mono::type_get_object) {
        dbg += "FOT=no_api ";
        return false;
    }

    MonoDomain* domain = mono::get_root_domain();
    if (!domain) {
        dbg += "FOT=no_domain ";
        return false;
    }

    // Create System.Type for ShowPlayerName
    MonoType* spn_mono_type = mono::class_get_type(s_ShowPlayerName_class);
    if (!spn_mono_type) {
        dbg += "FOT=no_type ";
        return false;
    }
    MonoObject* type_obj = mono::type_get_object(domain, spn_mono_type);
    if (!type_obj) {
        dbg += "FOT=no_typeobj ";
        return false;
    }

    // Call Object.FindObjectsOfType(type) - static method
    void* args[1] = { type_obj };
    MonoObject* exc = nullptr;
    MonoObject* result = mono::runtime_invoke(s_FindObjectsOfType, nullptr, args, &exc);
    if (exc || !result) {
        dbg += "FOT=invoke_fail ";
        return false;
    }

    // Result is MonoArray* (Object[])
    MonoArray* arr = reinterpret_cast<MonoArray*>(result);

    // Read array length (offset 0x0C on x86 Mono)
    uintptr_t count = 0;
    if (mono::array_length) {
        count = mono::array_length(arr);
    } else {
        count = *reinterpret_cast<uintptr_t*>(reinterpret_cast<char*>(arr) + 0x0C);
    }

    dbg += "FOT=" + std::to_string(count) + " ";
    if (count == 0) return false;

    // Array elements start at offset 0x10 on x86 Mono
    char* data_base = reinterpret_cast<char*>(arr) + 0x10;

    for (uintptr_t i = 0; i < count && i < 64; ++i) {
        MonoObject* spn_obj = *reinterpret_cast<MonoObject**>(data_base + i * sizeof(void*));
        if (!spn_obj) continue;

        GamePlayerData data{};

        // Read ShowName (string)
        if (s_SPN_ShowName) {
            MonoString* name_str = nullptr;
            mono::field_get_value(spn_obj, s_SPN_ShowName, &name_str);
            if (name_str) {
                char* utf8 = mono::string_to_utf8(name_str);
                if (utf8) {
                    data.name = utf8;
                    if (mono::free_mem) mono::free_mem(utf8);
                }
            }
        }
        // Fallback name: texClone (set by SetIconName)
        if (data.name.empty() && s_SPN_texClone) {
            MonoString* name_str = nullptr;
            mono::field_get_value(spn_obj, s_SPN_texClone, &name_str);
            if (name_str) {
                char* utf8 = mono::string_to_utf8(name_str);
                if (utf8) {
                    data.name = utf8;
                    if (mono::free_mem) mono::free_mem(utf8);
                }
            }
        }

        // Read PlayerID (PlayerSign struct: int playerID + byte serverID)
        if (s_SPN_PlayerID) {
            uint8_t ps_buf[32] = {};
            mono::field_get_value(spn_obj, s_SPN_PlayerID, ps_buf);
            data.player_id = *reinterpret_cast<int*>(ps_buf + 0);
            data.server_id = *reinterpret_cast<uint8_t*>(ps_buf + 4);
        }

        // Read isDead (bool)
        if (s_SPN_isDead) {
            mono_bool dead = 0;
            mono::field_get_value(spn_obj, s_SPN_isDead, &dead);
            data.is_dead = (dead != 0);
        }

        // Team detection via playerLayer (LayerEnum: Partner=9, Enemy=10)
        if (s_SPN_playerLayer) {
            int layer = 0;
            mono::field_get_value(spn_obj, s_SPN_playerLayer, &layer);
            data.raw_layer = layer;
            if (layer == 9) { // Partner = teammate
                data.team = local_team;
            } else if (layer == 10) { // Enemy
                data.team = (local_team == PlayerTeam::Red) ? PlayerTeam::Blue : PlayerTeam::Red;
            }
        }

        // Feet/body position from ShowPlayerName component's own transform
        // (component sits at the player's body/ground level)
        if (s_Component_get_transform) {
            MonoObject* own_tf = invoke_safe(s_Component_get_transform, spn_obj);
            if (own_tf) {
                data.world_position = read_transform_position(own_tf);
            }
        }

        // Head/nameplate position from ShowPlayerName.target Transform
        // (target is the nameplate point above the player's head)
        if (s_SPN_target) {
            MonoObject* target_tf = nullptr;
            mono::field_get_value(spn_obj, s_SPN_target, &target_tf);
            if (target_tf) {
                data.head_position = read_transform_position(target_tf);
            }
        }

        // If head_position is zero but world isn't, use world as fallback
        if (data.head_position.x == 0 && data.head_position.y == 0 &&
            data.head_position.z == 0) {
            data.head_position = data.world_position;
        }

        data.is_local = false;
        s_players.push_back(std::move(data));
    }

    return !s_players.empty();
}

// ---------------------------------------------------------------------------
// Helper: strip NGUI color tags from strings (e.g., "[00eaff]text[-]" → "text")
// ---------------------------------------------------------------------------
static std::string strip_ngui_tags(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '[') {
            // Find closing bracket
            size_t j = s.find(']', i + 1);
            if (j != std::string::npos) {
                i = j + 1;  // skip [...]
                continue;
            }
        }
        result += s[i];
        ++i;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Helper: read MonoString field from a MonoObject and return as std::string
// ---------------------------------------------------------------------------
static std::string read_mono_string_field(MonoObject* obj, MonoClassField* field) {
    if (!obj || !field) return {};
    MonoString* str = nullptr;
    mono::field_get_value(obj, field, &str);
    if (!str) return {};
    char* utf8 = mono::string_to_utf8(str);
    if (!utf8) return {};
    std::string result(utf8);
    if (mono::free_mem) mono::free_mem(utf8);
    return result;
}

// ---------------------------------------------------------------------------
// Helper: resolve player names from ServerDataTrans.roomCommandPlayerDict
// Reads Dict<PlayerSign, RoomCommandPlayer> valueSlots, uses
// RoomCommandPlayer.TargetPlayerID for matching (keySlots struct alignment
// is unreliable, so we read TargetPlayerID via field_get_value instead).
// ---------------------------------------------------------------------------
static void resolve_player_names() {
    if (!s_ServerDataTrans_class || !s_SDT_roomCmdPlayerDict ||
        !s_RCP_Nick || !s_RCP_TargetPlayerID)
        return;
    if (!mono::class_vtable || !mono::field_static_get_value)
        return;

    MonoDomain* domain = mono::get_root_domain();
    if (!domain) return;

    MonoVTable* sdt_vt = mono::class_vtable(domain, s_ServerDataTrans_class);
    if (!sdt_vt) return;

    MonoObject* dict = nullptr;
    mono::field_static_get_value(sdt_vt, s_SDT_roomCmdPlayerDict, &dict);
    if (!dict) return;

    // Read dictionary internals (only need linkSlots + valueSlots)
    char* dict_ptr = reinterpret_cast<char*>(dict);
    int count   = *reinterpret_cast<int*>(dict_ptr + DICT_OFF_COUNT);
    int touched = *reinterpret_cast<int*>(dict_ptr + DICT_OFF_TOUCHED_SLOTS);
    if (count <= 0 || touched <= 0) return;

    char* link_arr  = *reinterpret_cast<char**>(dict_ptr + DICT_OFF_LINK_SLOTS);
    char* value_arr = *reinterpret_cast<char**>(dict_ptr + DICT_OFF_VALUE_SLOTS);
    if (!link_arr || !value_arr) return;

    char* link_data  = link_arr  + MONO_ARRAY_DATA;
    char* value_data = value_arr + MONO_ARRAY_DATA;

    for (int i = 0; i < touched && i < 64; ++i) {
        int hash_code = *reinterpret_cast<int*>(link_data + i * LINK_STRUCT_SIZE);
        if ((hash_code & HASH_FLAG) == 0) continue;

        // Read RoomCommandPlayer object pointer from valueSlots
        MonoObject* rcp_obj = *reinterpret_cast<MonoObject**>(value_data + i * sizeof(void*));
        if (!rcp_obj) continue;

        // Read TargetPlayerID via field_get_value (lets Mono handle struct layout)
        uint8_t ps_buf[32] = {};
        mono::field_get_value(rcp_obj, s_RCP_TargetPlayerID, ps_buf);
        int tgt_pid = *reinterpret_cast<int*>(ps_buf + 0);
        uint8_t tgt_sid = *reinterpret_cast<uint8_t*>(ps_buf + 4);
        if (tgt_pid == 0) continue;

        // Read Nick
        std::string nick = read_mono_string_field(rcp_obj, s_RCP_Nick);
        if (nick.empty()) continue;

        // Strip NGUI color tags
        nick = strip_ngui_tags(nick);
        if (nick.empty()) continue;

        // Match to s_players by TargetPlayerID (player_id + server_id)
        for (auto& p : s_players) {
            if (p.player_id == tgt_pid && p.server_id == tgt_sid) {
                p.name = nick;
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// update
// ---------------------------------------------------------------------------
void update() {
    s_players.clear();
    s_local_index = -1;

    std::string dbg;

    // === Primary path: PlayerLocal.Instance -> player field ===
    MonoObject* local_player_obj = try_player_local_path(dbg);

    if (local_player_obj) {
        // Read the local player data directly
        GamePlayerData local_data{};
        if (read_player(local_player_obj, local_data)) {
            local_data.is_local = true;  // guaranteed local from PlayerLocal
            s_local_index = 0;
            s_players.push_back(std::move(local_data));

            // Append summary to debug info
            const auto& lp = s_players[0];
            dbg += "name=" + (lp.name.empty() ? "?" : lp.name) + " ";
            dbg += "hp=" + std::to_string(lp.blood) + "/" + std::to_string(lp.max_blood) + " ";
            dbg += "pos=(" +
                std::to_string((int)lp.world_position.x) + "," +
                std::to_string((int)lp.world_position.y) + "," +
                std::to_string((int)lp.world_position.z) + ") ";
            dbg += "dead=" + std::string(lp.is_dead ? "Y" : "N") + " ";
            dbg += "team=" + std::to_string((int)lp.team);
        } else {
            dbg += "read_player=FAIL ";
        }
    }

    // === Fallback/supplement: GameManager dictionary for all players ===
    // If PlayerLocal path failed, this is the only source of local player.
    // If it succeeded, this adds the remaining (remote) players.
    bool had_local_from_pl = (s_local_index >= 0);

    // Always try GM path to get all players (local + remote)
    // But first save the local player we already have
    GamePlayerData saved_local;
    if (had_local_from_pl) {
        saved_local = s_players[0];
    }

    // Temporarily clear for GM path population
    s_players.clear();
    s_local_index = -1;

    bool gm_ok = try_game_manager_path(dbg);

    if (gm_ok && had_local_from_pl) {
        // GM path succeeded and we have a PlayerLocal result.
        // Replace the GM's local player entry with the PlayerLocal one
        // (PlayerLocal is the authoritative source for the local player).
        for (int i = 0; i < static_cast<int>(s_players.size()); ++i) {
            if (s_players[i].is_local) {
                s_players[i] = saved_local;
                s_local_index = i;
                break;
            }
        }
        // If no local player found in GM list, insert it
        if (s_local_index < 0) {
            s_local_index = static_cast<int>(s_players.size());
            s_players.push_back(saved_local);
        }
    } else if (!gm_ok) {
        // GM path failed -- try FindObjectsOfType(ShowPlayerName) for remote players
        s_players.clear();
        s_local_index = -1;

        bool fot_ok = try_find_objects_path(dbg, saved_local.team);

        // Remove any FOT entry that matches local player (by player_id)
        if (had_local_from_pl && saved_local.player_id != 0) {
            s_players.erase(
                std::remove_if(s_players.begin(), s_players.end(),
                    [&](const GamePlayerData& p) {
                        return p.player_id == saved_local.player_id;
                    }),
                s_players.end());
        }

        // Insert local player at beginning
        if (had_local_from_pl) {
            s_players.insert(s_players.begin(), saved_local);
            s_local_index = 0;
            if (!fot_ok) {
                dbg += "| GM+FOT_fail,PL_only ";
            }
        } else if (!fot_ok) {
            dbg += "| ERR: all_paths_failed";
        }
    }

    // === Resolve player names from ServerDataTrans.roomCommandPlayerDict ===
    resolve_player_names();

    // === Cache local player HP diagnostic ===
    const GamePlayerData* lp = get_local_player();
    if (lp) {
        s_local_hp_diag = lp->hp_diag;
    } else {
        s_local_hp_diag = {};
    }

    // === HP logging to file ===
    if (s_hp_logging && lp) {
        if (++s_hp_log_counter >= 60) {  // ~1 second at 60fps
            s_hp_log_counter = 0;
            s_hp_log_line++;

            const HPDiagnostic& d = s_local_hp_diag;

            // Open file in append mode
            FILE* fp = fopen(s_hp_log_path.c_str(), "a");
            if (fp) {
                // Write header on first line
                if (s_hp_log_line == 1) {
                    fprintf(fp, "Tick  | Used | A:BPI.Hp  A:Max   | B:Sing.Hp B:Max   "
                                "| C:Helth   C:Max   | D:blood   D:max   "
                                "| E:slider  | F:label   | Final\n");
                    for (int i = 0; i < 140; ++i) fputc('-', fp);
                    fputc('\n', fp);
                }

                fprintf(fp,
                    "%-5d | %c    | %-9d %-7d | %-9d %-7d | %-9d %-7d | %-9d %-7d | %-9.4f | %-9s | %d/%d\n",
                    s_hp_log_line,
                    d.used_source,
                    d.src_a_hp, d.src_a_maxhp,
                    d.src_b_hp, d.src_b_maxhp,
                    d.src_c_hp, d.src_c_maxhp,
                    d.src_d_hp, d.src_d_maxhp,
                    d.src_e_valid ? d.src_e_slider : -1.0f,
                    d.src_f_valid ? d.src_f_label : "N/A",
                    lp->blood, lp->max_blood);
                fclose(fp);
            }
        }
    }

    s_debug_info = dbg;
}

// ---------------------------------------------------------------------------
// get_players
// ---------------------------------------------------------------------------
const std::vector<GamePlayerData>& get_players() {
    return s_players;
}

// ---------------------------------------------------------------------------
// get_local_player
// ---------------------------------------------------------------------------
const GamePlayerData* get_local_player() {
    if (s_local_index >= 0 && s_local_index < static_cast<int>(s_players.size()))
        return &s_players[s_local_index];
    return nullptr;
}

// ---------------------------------------------------------------------------
// get_debug_info
// ---------------------------------------------------------------------------
const std::string& get_debug_info() {
    return s_debug_info;
}

// ---------------------------------------------------------------------------
// dump_all_data
// ---------------------------------------------------------------------------
std::string dump_all_data() {
    std::ostringstream ss;

    ss << "=== game_data dump ===\n";
    ss << "debug: " << s_debug_info << "\n";
    ss << "player_count: " << s_players.size() << "\n";
    ss << "local_index: " << s_local_index << "\n\n";

    auto team_str = [](PlayerTeam t) -> const char* {
        switch (t) {
            case PlayerTeam::Red:    return "Red";
            case PlayerTeam::Blue:   return "Blue";
            default:                 return "NoTeam";
        }
    };

    for (size_t i = 0; i < s_players.size(); ++i) {
        const auto& p = s_players[i];
        ss << "--- Player[" << i << "] ---\n";
        ss << "  name:       " << (p.name.empty() ? "<empty>" : p.name) << "\n";
        ss << "  player_id:  " << p.player_id << "\n";
        ss << "  server_id:  " << (int)p.server_id << "\n";
        ss << "  blood:      " << p.blood << " / " << p.max_blood << "\n";
        ss << "  is_dead:    " << (p.is_dead ? "true" : "false") << "\n";
        ss << "  is_local:   " << (p.is_local ? "true" : "false") << "\n";
        ss << "  team:       " << team_str(p.team) << "\n";
        ss << "  raw_layer:  " << p.raw_layer << "\n";

        ss << std::fixed << std::setprecision(2);
        ss << "  world_pos:  (" << p.world_position.x << ", "
           << p.world_position.y << ", " << p.world_position.z << ")\n";
        ss << "  head_pos:   (" << p.head_position.x << ", "
           << p.head_position.y << ", " << p.head_position.z << ")\n";
        ss << "\n";
    }

    // Cache status for diagnostics
    ss << "=== Cache Status ===\n";
    ss << "  PlayerLocal class: " << (s_PlayerLocal_class ? "OK" : "null") << "\n";
    ss << "  PL.get_Instance:   " << (s_PL_get_Instance ? "OK" : "null") << "\n";
    ss << "  PL.player field:   " << (s_PL_player_field ? "OK" : "null") << "\n";
    ss << "  GameManager class: " << (s_GameManager_class ? "OK" : "null") << "\n";
    ss << "  GM.get_Instance:   " << (s_GM_get_Instance ? "OK" : "null") << "\n";
    ss << "  GM.get_Players:    " << (s_GM_get_Players ? "OK" : "null") << "\n";
    ss << "  Player class:      " << (s_Player_class ? "OK" : "null") << "\n";
    ss << "  Transform class:   " << (s_Transform_class ? "OK" : "null") << "\n";
    ss << "  GameObject class:  " << (s_GameObject_class ? "OK" : "null") << "\n";
    ss << "  Dict methods:      " << (s_Dict_get_Count ? "OK" : "not resolved") << "\n";
    ss << "  BattlePlayerInfo:  " << (s_BattlePlayerInfo_class ? "OK" : "null") << "\n";
    ss << "  BPI.Hp field:      " << (s_BPI_Hp ? "OK" : "null") << "\n";
    ss << "  BPI.MaxHP field:   " << (s_BPI_MaxHP ? "OK" : "null") << "\n";
    ss << "  BPI.singleton:     " << (s_BPI_get_singleton ? "OK" : "null") << "\n";
    ss << "  ShowPlayerName:    " << (s_ShowPlayerName_class ? "OK" : "null") << "\n";
    ss << "  SPN.ShowName:      " << (s_SPN_ShowName ? "OK" : "null") << "\n";
    ss << "  Player.battlePlayer: " << (s_Player_battlePlayer ? "OK" : "null") << "\n";
    ss << "  Player.showName:   " << (s_Player_showName ? "OK" : "null") << "\n";
    ss << "  FindObjectsOfType: " << (s_FindObjectsOfType ? "OK" : "null") << "\n";
    ss << "  Comp.get_transform:" << (s_Component_get_transform ? "OK" : "null") << "\n";
    ss << "  SPN.PlayerID:      " << (s_SPN_PlayerID ? "OK" : "null") << "\n";
    ss << "  SPN.isDead:        " << (s_SPN_isDead ? "OK" : "null") << "\n";
    ss << "  SPN.target:        " << (s_SPN_target ? "OK" : "null") << "\n";
    ss << "  SPN.playerLayer:   " << (s_SPN_playerLayer ? "OK" : "null") << "\n";
    ss << "  SPN.texClone:      " << (s_SPN_texClone ? "OK" : "null") << "\n";
    ss << "  class_get_type:    " << (mono::class_get_type ? "OK" : "null") << "\n";
    ss << "  type_get_object:   " << (mono::type_get_object ? "OK" : "null") << "\n";
    ss << "  ServerDataTrans:   " << (s_ServerDataTrans_class ? "OK" : "null") << "\n";
    ss << "  SDT.roomCmdDict:   " << (s_SDT_roomCmdPlayerDict ? "OK" : "null") << "\n";
    ss << "  RoomCmdPlayer:     " << (s_RoomCmdPlayer_class ? "OK" : "null") << "\n";
    ss << "  RCP.Nick:          " << (s_RCP_Nick ? "OK" : "null") << "\n";
    ss << "  RCP.TargetPID:     " << (s_RCP_TargetPlayerID ? "OK" : "null") << "\n";

    // === RUNTIME FIELD PROBE ===
    // Probe the actual runtime objects on the local Player to diagnose null chains
    ss << "\n=== Runtime Field Probe ===\n";
    do {
        // Get PlayerLocal.Instance
        MonoObject* pl_inst = s_PL_get_Instance ? invoke_safe(s_PL_get_Instance) : nullptr;
        ss << "  PL.Instance:       " << (pl_inst ? "EXISTS" : "NULL") << "\n";
        if (!pl_inst) break;

        // Get player from PlayerLocal
        MonoObject* player_obj = nullptr;
        if (s_PL_player_field) mono::field_get_value(pl_inst, s_PL_player_field, &player_obj);
        ss << "  PL.player:         " << (player_obj ? "EXISTS" : "NULL") << "\n";
        if (!player_obj) break;

        // Probe each sub-field on the Player
        auto probe_obj_field = [&](const char* label, MonoClassField* field) {
            if (!field) { ss << "  " << label << " FIELD_HANDLE=NULL\n"; return (MonoObject*)nullptr; }
            MonoObject* val = nullptr;
            mono::field_get_value(player_obj, field, &val);
            ss << "  " << label << " " << (val ? "EXISTS" : "NULL") << "\n";
            return val;
        };

        MonoObject* pt = probe_obj_field("PlayerTransform:", s_Player_PlayerTransform);
        MonoObject* eye = probe_obj_field("eye (GameObject):", s_Player_eye);
        MonoObject* bp = probe_obj_field("battlePlayer:   ", s_Player_battlePlayer);
        MonoObject* sn = probe_obj_field("showName:       ", s_Player_showName);
        probe_obj_field("playerRemote:   ", s_Player_playerRemote);

        // Read PlayerName string directly
        if (s_Player_PlayerName) {
            MonoString* pn = nullptr;
            mono::field_get_value(player_obj, s_Player_PlayerName, &pn);
            if (pn) {
                char* utf8 = mono::string_to_utf8(pn);
                ss << "  PlayerName str:    \"" << (utf8 ? utf8 : "") << "\"\n";
                if (utf8 && mono::free_mem) mono::free_mem(utf8);
            } else {
                ss << "  PlayerName str:    NULL\n";
            }
        }

        // Read blood/maxblood directly
        int b = 0, mb = 0;
        if (s_Player_blood) mono::field_get_value(player_obj, s_Player_blood, &b);
        if (s_Player_maxblood) mono::field_get_value(player_obj, s_Player_maxblood, &mb);
        ss << "  Player.blood:      " << b << " / " << mb << "\n";

        // If battlePlayer exists, read its HP
        if (bp) {
            int bpi_hp = 0, bpi_mhp = 0;
            if (s_BPI_Hp) mono::field_get_value(bp, s_BPI_Hp, &bpi_hp);
            if (s_BPI_MaxHP) mono::field_get_value(bp, s_BPI_MaxHP, &bpi_mhp);
            ss << "  BPI.Hp/MaxHP:      " << bpi_hp << " / " << bpi_mhp << "\n";
            if (s_BPI_isAlive) {
                mono_bool alive = 0;
                mono::field_get_value(bp, s_BPI_isAlive, &alive);
                ss << "  BPI.isAlive:       " << (alive ? "true" : "false") << "\n";
            }
            if (s_BPI_pos) {
                unity::Vector3 bpi_pos{};
                mono::field_get_value(bp, s_BPI_pos, &bpi_pos);
                ss << std::fixed << std::setprecision(2);
                ss << "  BPI.pos:           (" << bpi_pos.x << ", " << bpi_pos.y << ", " << bpi_pos.z << ")\n";
            }
        }

        // Try BPI singleton
        if (s_BPI_get_singleton) {
            MonoObject* bpi_s = invoke_safe(s_BPI_get_singleton);
            ss << "  BPI singleton:     " << (bpi_s ? "EXISTS" : "NULL") << "\n";
            if (bpi_s) {
                int sh = 0, smh = 0;
                if (s_BPI_Hp) mono::field_get_value(bpi_s, s_BPI_Hp, &sh);
                if (s_BPI_MaxHP) mono::field_get_value(bpi_s, s_BPI_MaxHP, &smh);
                ss << "  BPI_S.Hp/MaxHP:    " << sh << " / " << smh << "\n";
                if (s_BPI_pos) {
                    unity::Vector3 sp{};
                    mono::field_get_value(bpi_s, s_BPI_pos, &sp);
                    ss << std::fixed << std::setprecision(2);
                    ss << "  BPI_S.pos:         (" << sp.x << ", " << sp.y << ", " << sp.z << ")\n";
                }
                if (s_BPI_isAlive) {
                    mono_bool sa = 0;
                    mono::field_get_value(bpi_s, s_BPI_isAlive, &sa);
                    ss << "  BPI_S.isAlive:     " << (sa ? "true" : "false") << "\n";
                }
            }
        }

        // If showName exists, read its ShowName
        if (sn && s_SPN_ShowName) {
            MonoString* sn_str = nullptr;
            mono::field_get_value(sn, s_SPN_ShowName, &sn_str);
            if (sn_str) {
                char* utf8 = mono::string_to_utf8(sn_str);
                ss << "  SPN.ShowName:      \"" << (utf8 ? utf8 : "") << "\"\n";
                if (utf8 && mono::free_mem) mono::free_mem(utf8);
            } else {
                ss << "  SPN.ShowName:      NULL\n";
            }
        }

        // Check GameManager dict count (both invoke and direct memory)
        if (s_GM_get_Instance && s_GM_get_Players) {
            MonoObject* gm = invoke_safe(s_GM_get_Instance);
            ss << "  GM.Instance:       " << (gm ? "EXISTS" : "NULL") << "\n";
            if (gm) {
                MonoObject* dict = invoke_safe(s_GM_get_Players, gm);
                ss << "  GM.Players dict:   " << (dict ? "EXISTS" : "NULL") << "\n";
                if (dict) {
                    // Direct memory read (reliable)
                    char* dp = reinterpret_cast<char*>(dict);
                    int mem_count   = *reinterpret_cast<int*>(dp + DICT_OFF_COUNT);
                    int mem_touched = *reinterpret_cast<int*>(dp + DICT_OFF_TOUCHED_SLOTS);
                    ss << "  Dict.count(mem):   " << mem_count << "\n";
                    ss << "  Dict.touched(mem): " << mem_touched << "\n";

                    // Invoke-based (may fail from non-game thread)
                    if (s_Dict_get_Count) {
                        MonoObject* cnt = invoke_safe(s_Dict_get_Count, dict);
                        if (cnt) {
                            int c = *static_cast<int*>(mono::object_unbox(cnt));
                            ss << "  Dict.Count(invoke):" << c << "\n";
                        }
                    }
                }
            }
        }

        // === NEW DATA SOURCES PROBE ===
        ss << "\n--- New Data Sources ---\n";

        // DataCenter.rPlayer -> 角色 -> 昵称
        ss << "  DataCenter class:  " << (s_DataCenter_class ? "OK" : "null") << "\n";
        ss << "  DC.rPlayer field:  " << (s_DC_rPlayer ? "OK" : "null") << "\n";
        ss << "  PlayerInfo class:  " << (s_PlayerInfo_class ? "OK" : "null") << "\n";
        ss << "  PI.角色 field:     " << (s_PI_角色 ? "OK" : "null") << "\n";
        ss << "  角色 class:        " << (s_角色_class ? "OK" : "null") << "\n";
        ss << "  角色.昵称 field:   " << (s_角色_昵称 ? "OK" : "null") << "\n";
        ss << "  vtable API:        " << (mono::class_vtable ? "OK" : "null") << "\n";
        ss << "  static_get_value:  " << (mono::field_static_get_value ? "OK" : "null") << "\n";

        if (s_DC_rPlayer && mono::class_vtable && mono::field_static_get_value) {
            MonoDomain* dom = mono::get_root_domain();
            MonoVTable* vt = dom ? mono::class_vtable(dom, s_DataCenter_class) : nullptr;
            ss << "  DC vtable:         " << (vt ? "EXISTS" : "NULL") << "\n";
            if (vt) {
                MonoObject* rp = nullptr;
                mono::field_static_get_value(vt, s_DC_rPlayer, &rp);
                ss << "  DC.rPlayer obj:    " << (rp ? "EXISTS" : "NULL") << "\n";
                if (rp && s_PI_角色) {
                    MonoObject* role = nullptr;
                    mono::field_get_value(rp, s_PI_角色, &role);
                    ss << "  rPlayer.角色:      " << (role ? "EXISTS" : "NULL") << "\n";
                    if (role && s_角色_昵称) {
                        MonoString* nick = nullptr;
                        mono::field_get_value(role, s_角色_昵称, &nick);
                        if (nick) {
                            char* u = mono::string_to_utf8(nick);
                            ss << "  角色.昵称:         \"" << (u ? u : "") << "\"\n";
                            if (u && mono::free_mem) mono::free_mem(u);
                        } else {
                            ss << "  角色.昵称:         NULL\n";
                        }
                    }
                }
            }
        }

        // NGUI_BattleManager -> helth -> HP
        ss << "  NGUI_BM class:     " << (s_NGUI_BM_class ? "OK" : "null") << "\n";
        ss << "  NGUI_BM.Instance:  " << (s_NGUI_BM_get_Instance ? "OK" : "null") << "\n";
        ss << "  Helth class:       " << (s_NGUI_Helth_class ? "OK" : "null") << "\n";
        if (s_NGUI_BM_get_Instance) {
            MonoObject* bm = invoke_safe(s_NGUI_BM_get_Instance);
            ss << "  BM instance:       " << (bm ? "EXISTS" : "NULL") << "\n";
            if (bm && s_NGUI_BM_helth) {
                MonoObject* h = nullptr;
                mono::field_get_value(bm, s_NGUI_BM_helth, &h);
                ss << "  BM.helth:          " << (h ? "EXISTS" : "NULL") << "\n";
                if (h && s_Helth_currentHP && s_Helth_maxHP) {
                    int ch = 0, mh = 0;
                    mono::field_get_value(h, s_Helth_currentHP, &ch);
                    mono::field_get_value(h, s_Helth_maxHP, &mh);
                    ss << "  Helth HP:          " << ch << " / " << mh << "\n";
                }
            }
        }

        // PlayerLocal.mainCamera
        if (s_PL_mainCamera) {
            MonoObject* cam = nullptr;
            mono::field_get_value(pl_inst, s_PL_mainCamera, &cam);
            ss << "  PL.mainCamera:     " << (cam ? "EXISTS" : "NULL") << "\n";
            if (cam && s_GO_get_transform) {
                MonoObject* cam_tf = invoke_safe(s_GO_get_transform, cam);
                if (cam_tf) {
                    unity::Vector3 cp = read_transform_position(cam_tf);
                    ss << std::fixed << std::setprecision(2);
                    ss << "  Camera pos:        (" << cp.x << ", " << cp.y << ", " << cp.z << ")\n";
                }
            }
        }
    } while (false);

    return ss.str();
}

// ---------------------------------------------------------------------------
// get_local_hp_diagnostic
// ---------------------------------------------------------------------------
const HPDiagnostic& get_local_hp_diagnostic() {
    return s_local_hp_diag;
}

// ---------------------------------------------------------------------------
// HP logging
// ---------------------------------------------------------------------------
void start_hp_logging() {
    if (s_hp_logging) return;

    const char* userprofile = getenv("USERPROFILE");
    if (userprofile) {
        s_hp_log_path = std::string(userprofile) + "\\Desktop\\firerush_hp_log.txt";
    } else {
        char tmp[MAX_PATH];
        GetTempPathA(MAX_PATH, tmp);
        s_hp_log_path = std::string(tmp) + "firerush_hp_log.txt";
    }

    // Clear old log
    FILE* fp = fopen(s_hp_log_path.c_str(), "w");
    if (fp) fclose(fp);

    s_hp_logging     = true;
    s_hp_log_counter = 0;
    s_hp_log_line    = 0;
}

void stop_hp_logging() {
    s_hp_logging = false;
}

bool is_hp_logging() {
    return s_hp_logging;
}

// ---------------------------------------------------------------------------
// shutdown
// ---------------------------------------------------------------------------
void shutdown() {
    s_hp_logging = false;
    s_players.clear();
    s_local_index = -1;
    s_debug_info.clear();

    s_PlayerLocal_class = nullptr;
    s_GameManager_class = nullptr;
    s_Player_class      = nullptr;
    s_Transform_class   = nullptr;
    s_GameObject_class  = nullptr;

    s_PL_get_Instance   = nullptr;
    s_PL_player_field   = nullptr;

    s_GM_get_Instance   = nullptr;
    s_GM_get_Players    = nullptr;

    s_Dict_get_Count          = nullptr;
    s_Dict_get_Values         = nullptr;
    s_ValueColl_GetEnumerator = nullptr;
    s_Enumerator_MoveNext     = nullptr;
    s_Enumerator_get_Current  = nullptr;
    s_Enumerator_Dispose      = nullptr;

    s_Player_PlayerID        = nullptr;
    s_Player_PlayerName      = nullptr;
    s_Player_PlayerTransform = nullptr;
    s_Player_blood           = nullptr;
    s_Player_maxblood        = nullptr;
    s_Player_isDead          = nullptr;
    s_Player_localPlayer     = nullptr;
    s_Player_playerTeam      = nullptr;
    s_Player_eye             = nullptr;
    s_Player_battlePlayer    = nullptr;
    s_Player_showName        = nullptr;

    s_BattlePlayerInfo_class = nullptr;
    s_BPI_Hp                 = nullptr;
    s_BPI_MaxHP              = nullptr;
    s_BPI_pos                = nullptr;
    s_BPI_isAlive            = nullptr;
    s_BPI_PlayerTeam         = nullptr;
    s_BPI_get_singleton      = nullptr;

    s_ShowPlayerName_class   = nullptr;
    s_SPN_ShowName           = nullptr;

    s_Player_playerRemote    = nullptr;

    s_DataCenter_class       = nullptr;
    s_DC_rPlayer             = nullptr;
    s_DC_CurrentPlayerSign   = nullptr;
    s_PlayerInfo_class       = nullptr;
    s_PI_角色                = nullptr;
    s_角色_class             = nullptr;
    s_角色_昵称              = nullptr;

    s_ServerDataTrans_class   = nullptr;
    s_SDT_roomCmdPlayerDict   = nullptr;
    s_RoomCmdPlayer_class     = nullptr;
    s_RCP_Nick                = nullptr;
    s_RCP_TargetPlayerID      = nullptr;

    s_NGUI_BM_class          = nullptr;
    s_NGUI_BM_get_Instance   = nullptr;
    s_NGUI_BM_helth          = nullptr;
    s_NGUI_Helth_class       = nullptr;
    s_Helth_currentHP        = nullptr;
    s_Helth_maxHP            = nullptr;
    s_Helth_hpSlider         = nullptr;
    s_Helth_hpLabel          = nullptr;
    s_UISlider_mValue        = nullptr;
    s_UILabel_mText          = nullptr;
    s_ui_fields_resolved     = false;

    s_PL_mainCamera          = nullptr;

    s_UE_Object_class        = nullptr;
    s_Component_class        = nullptr;
    s_FindObjectsOfType      = nullptr;
    s_Component_get_transform= nullptr;
    s_SPN_PlayerID           = nullptr;
    s_SPN_isDead             = nullptr;
    s_SPN_target             = nullptr;
    s_SPN_playerLayer        = nullptr;
    s_SPN_texClone           = nullptr;

    s_Transform_get_position = nullptr;
    s_GO_get_transform       = nullptr;
}

} // namespace game_data
