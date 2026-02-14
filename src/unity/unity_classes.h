#pragma once

#include "../mono/mono_types.h"

namespace unity {

// Cached Mono image handles for the assemblies we care about.
// firerush (Unity 4.x) uses a single UnityEngine.dll for everything.
struct CachedImages {
    MonoImage* unity_engine    = nullptr;  // UnityEngine (single DLL in Unity 4.x)
    MonoImage* imgui_module    = nullptr;  // Same as unity_engine on Unity 4.x
    MonoImage* input_module    = nullptr;  // Same as unity_engine on Unity 4.x
    MonoImage* assembly_csharp = nullptr;  // Assembly-CSharp
};

// Cached MonoClass* handles for Unity engine classes (GUI-only subset)
struct CachedClasses {
    MonoClass* GUI            = nullptr;
    MonoClass* GUILayout      = nullptr;
    MonoClass* GUIStyle       = nullptr;
    MonoClass* GUISkin        = nullptr;
    MonoClass* GUIContent     = nullptr;
    MonoClass* GUIUtility     = nullptr;
    MonoClass* Rect           = nullptr;
    MonoClass* Screen         = nullptr;
    MonoClass* GameObject     = nullptr;
    MonoClass* Object         = nullptr;  // UnityEngine.Object
    MonoClass* MonoBehaviour  = nullptr;
    MonoClass* Input          = nullptr;
    MonoClass* KeyCode        = nullptr;
    MonoClass* Color          = nullptr;
    MonoClass* Texture2D      = nullptr;
};

// Cached MonoMethod* handles for frequently-used Unity methods (GUI-only subset)
struct CachedMethods {
    // ---- GUI static methods ----
    MonoMethod* GUI_Label_2               = nullptr;  // GUI.Label(Rect, string)
    MonoMethod* GUI_Box_2                 = nullptr;  // GUI.Box(Rect, string)
    MonoMethod* GUI_Button_2              = nullptr;  // GUI.Button(Rect, string) -> bool
    MonoMethod* GUI_Toggle_3              = nullptr;  // GUI.Toggle(Rect, bool, string) -> bool
    MonoMethod* GUI_HorizontalSlider_4    = nullptr;  // GUI.HorizontalSlider(Rect, float, float, float) -> float
    MonoMethod* GUI_TextField_2           = nullptr;  // GUI.TextField(Rect, string) -> string
    MonoMethod* GUI_DragWindow_0          = nullptr;  // GUI.DragWindow()

    // ---- GUILayout static methods ----
    MonoMethod* GUILayout_Label_1         = nullptr;
    MonoMethod* GUILayout_Button_1        = nullptr;
    MonoMethod* GUILayout_Toggle_2        = nullptr;
    MonoMethod* GUILayout_BeginVertical_0   = nullptr;
    MonoMethod* GUILayout_EndVertical_0     = nullptr;
    MonoMethod* GUILayout_BeginHorizontal_0 = nullptr;
    MonoMethod* GUILayout_EndHorizontal_0   = nullptr;
    MonoMethod* GUILayout_BeginArea_1     = nullptr;
    MonoMethod* GUILayout_EndArea_0       = nullptr;
    MonoMethod* GUILayout_Space_1         = nullptr;
    MonoMethod* GUILayout_FlexibleSpace_0 = nullptr;

    // ---- GUIStyle ----
    MonoMethod* GUIStyle_set_fontSize     = nullptr;
    MonoMethod* GUIStyle_set_alignment    = nullptr;
    MonoMethod* GUIStyle_get_fontSize     = nullptr;
    MonoMethod* GUIStyle_get_alignment    = nullptr;

    // ---- Screen ----
    MonoMethod* Screen_get_width          = nullptr;
    MonoMethod* Screen_get_height         = nullptr;

    // ---- Input ----
    MonoMethod* Input_GetKeyDown          = nullptr;
    MonoMethod* Input_GetKey              = nullptr;
    MonoMethod* Input_GetMouseButton      = nullptr;
    MonoMethod* Input_GetMouseButtonDown  = nullptr;
    MonoMethod* Input_get_mousePosition   = nullptr;

    // ---- GUI drawing / color ----
    MonoMethod* GUI_DrawTexture_2         = nullptr;
    MonoMethod* GUI_set_color             = nullptr;
    MonoMethod* GUI_get_color             = nullptr;

    // ---- GUI skin / style ----
    MonoMethod* GUI_get_skin              = nullptr;
    MonoMethod* GUISkin_get_label         = nullptr;

    // ---- Texture2D ----
    MonoMethod* Texture2D_ctor_2          = nullptr;
    MonoMethod* Texture2D_SetPixel        = nullptr;
    MonoMethod* Texture2D_Apply_0         = nullptr;

    // ---- Object ----
    MonoMethod* Object_get_name           = nullptr;
    MonoMethod* Object_DontDestroyOnLoad  = nullptr;
    MonoMethod* Object_set_hideFlags      = nullptr;
};

// Initialize the class/method cache. Returns false if critical classes are missing.
bool cache_initialize();

// Release cached references (safe to call multiple times).
void cache_shutdown();

// Accessors for the global cached data.
CachedImages&  images();
CachedClasses& classes();
CachedMethods& methods();

} // namespace unity
