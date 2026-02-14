#include "pch.h"
#include "core/globals.h"
#include "core/game_data.h"
#include "mono/mono_api.h"
#include "unity/unity_classes.h"
#include "gui/gui.h"
#include "gui/esp.h"
#include "menu/menu.h"
#include "cheats/cheats.h"
#include "payload/payload.h"

static DWORD WINAPI init_thread(LPVOID param)
{
    // Wait for mono.dll to be loaded by the game
    while (!GetModuleHandleA("mono.dll")) {
        Sleep(100);
    }
    Sleep(2000); // Wait for Unity to fully initialize

    // Step 1: Initialize Mono API
    if (!mono::initialize()) {
        MessageBoxA(nullptr, "Failed to initialize Mono API", "FireRush-Internal", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Step 2: Attach to Mono domain
    MonoDomain* domain = mono::get_root_domain();
    if (!domain) {
        MessageBoxA(nullptr, "Failed to get root domain", "FireRush-Internal", MB_OK | MB_ICONERROR);
        return 1;
    }
    mono::thread_attach(domain);

    // Step 3: Cache Unity classes and methods
    if (!unity::cache_initialize()) {
        MessageBoxA(nullptr, "Failed to cache Unity classes", "FireRush-Internal", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Step 4: Initialize GUI wrappers
    gui::initialize();

    // Step 5: Initialize menu
    menu::initialize();

    // Step 6: Initialize game data reader
    if (!game_data::initialize()) {
        MessageBoxA(nullptr, "Failed to initialize game data reader", "FireRush-Internal", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Step 7: Initialize cheats module
    if (!cheats::initialize()) {
        MessageBoxA(nullptr, "Failed to initialize cheats module", "FireRush-Internal", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Step 8: Initialize ESP renderer
    if (!esp::initialize()) {
        MessageBoxA(nullptr, "Failed to initialize ESP renderer", "FireRush-Internal", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Step 9: Load and execute C# payload
    if (!payload::initialize()) {
        MessageBoxA(nullptr, "Failed to load payload", "FireRush-Internal", MB_OK | MB_ICONERROR);
        return 1;
    }

    globals::initialized = true;

    // Keep thread alive, wait for unload signal.
    // Also monitor heartbeat -- if C# callbacks stop for too long,
    // re-inject the MonoBehaviour overlay.
    uint64_t last_heartbeat      = payload::get_heartbeat();
    DWORD    last_heartbeat_tick  = GetTickCount();
    constexpr DWORD HEARTBEAT_TIMEOUT_MS = 8000;

    while (globals::running) {
        // Press END key to unload
        if (GetAsyncKeyState(VK_END) & 1) {
            globals::running = false;
        }

        // Watchdog: check if heartbeat is advancing
        uint64_t hb = payload::get_heartbeat();
        if (hb != last_heartbeat) {
            last_heartbeat     = hb;
            last_heartbeat_tick = GetTickCount();
        } else {
            DWORD elapsed = GetTickCount() - last_heartbeat_tick;
            if (elapsed > HEARTBEAT_TIMEOUT_MS && last_heartbeat > 0) {
                payload::reinject();
                last_heartbeat_tick = GetTickCount();
                Sleep(500);
                last_heartbeat = payload::get_heartbeat();
            }
        }

        Sleep(100);
    }

    // Cleanup
    payload::shutdown();
    cheats::shutdown();
    game_data::shutdown();
    unity::cache_shutdown();
    mono::shutdown();

    // Unload the DLL
    HMODULE check = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)globals::dll_handle, &check) && check) {
        FreeLibraryAndExitThread(globals::dll_handle, 0);
    } else {
        ExitThread(0);
    }
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        globals::dll_handle = hModule;
        CreateThread(nullptr, 0, init_thread, nullptr, 0, nullptr);
    }
    return TRUE;
}
