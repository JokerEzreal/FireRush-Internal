#include "payload.h"
#include "payload_bytes.h"

#include "../mono/mono_api.h"
#include "../mono/mono_types.h"
#include "../menu/menu.h"

#include <cstdio>
#include <atomic>

// ---------------------------------------------------------------------------
// Heartbeat counter -- proves that C# callbacks are running.
// ---------------------------------------------------------------------------
static std::atomic<uint64_t> s_heartbeat{0};

// Cached handles for re-injection
static MonoMethod* s_loader_init = nullptr;

// ---------------------------------------------------------------------------
// C++ callbacks invoked from C# via Mono internal calls.
// ---------------------------------------------------------------------------

// Called from C# OnGUI -- drawing the menu
static void __cdecl on_draw_menu()
{
    s_heartbeat.fetch_add(1, std::memory_order_relaxed);
    menu::on_gui_menu();
}

// Called from C# Update -- per-frame logic
static void __cdecl on_update_callback()
{
    menu::on_update();
}

// ---------------------------------------------------------------------------
// payload::initialize
// ---------------------------------------------------------------------------

namespace payload {

bool initialize()
{
    // -----------------------------------------------------------------------
    // Step 1 -- Register internal calls (must match C# Bridge method names)
    // -----------------------------------------------------------------------
    mono::add_internal_call("Payload.Bridge::OnDrawMenu",
                            reinterpret_cast<const void*>(on_draw_menu));
    mono::add_internal_call("Payload.Bridge::OnUpdateCallback",
                            reinterpret_cast<const void*>(on_update_callback));

    // -----------------------------------------------------------------------
    // Step 2 -- Load embedded C# assembly
    // -----------------------------------------------------------------------
    MonoDomain* domain = mono::get_root_domain();
    if (!domain) {
        printf("[payload] ERROR: mono_get_root_domain returned nullptr\n");
        return false;
    }

    mono::thread_attach(domain);

    MonoImageOpenStatus status = MONO_IMAGE_OK;

    MonoImage* image = mono::image_open_from_data_full(
        const_cast<char*>(reinterpret_cast<const char*>(g_payload_dll_bytes)),
        static_cast<uint32_t>(g_payload_dll_size),
        /*need_copy=*/1,
        &status,
        /*refonly=*/0);

    if (!image || status != MONO_IMAGE_OK) {
        printf("[payload] ERROR: mono_image_open_from_data_full failed (status %d)\n",
               static_cast<int>(status));
        return false;
    }

    MonoAssembly* assembly = mono::assembly_load_from_full(
        image,
        "PayloadAssembly",
        &status,
        /*refonly=*/0);

    if (!assembly || status != MONO_IMAGE_OK) {
        printf("[payload] ERROR: mono_assembly_load_from_full failed (status %d)\n",
               static_cast<int>(status));
        mono::image_close(image);
        return false;
    }

    // -----------------------------------------------------------------------
    // Step 3 -- Invoke Payload.Loader.Init()
    // -----------------------------------------------------------------------
    MonoImage* payload_image = mono::assembly_get_image(assembly);
    if (!payload_image) {
        printf("[payload] ERROR: mono_assembly_get_image returned nullptr\n");
        return false;
    }

    MonoClass* loader_class = mono::class_from_name(payload_image, "Payload", "Loader");
    if (!loader_class) {
        printf("[payload] ERROR: could not find class Payload.Loader\n");
        return false;
    }

    MonoMethod* init_method = mono::class_get_method_from_name(loader_class, "Init", 0);
    if (!init_method) {
        printf("[payload] ERROR: could not find method Payload.Loader::Init()\n");
        return false;
    }

    // Cache for re-injection
    s_loader_init = init_method;

    MonoObject* exception = nullptr;
    mono::runtime_invoke(init_method, nullptr, nullptr, &exception);

    if (exception) {
        printf("[payload] ERROR: Payload.Loader.Init() threw an exception\n");
        return false;
    }

    printf("[payload] C# payload loaded and Loader.Init() invoked successfully\n");
    return true;
}

// ---------------------------------------------------------------------------
// payload::get_heartbeat
// ---------------------------------------------------------------------------

uint64_t get_heartbeat()
{
    return s_heartbeat.load(std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// payload::reinject
// ---------------------------------------------------------------------------

bool reinject()
{
    if (!s_loader_init) {
        printf("[payload] reinject: no cached Init method\n");
        return false;
    }

    MonoDomain* domain = mono::get_root_domain();
    if (domain) {
        mono::thread_attach(domain);
    }

    MonoObject* exception = nullptr;
    mono::runtime_invoke(s_loader_init, nullptr, nullptr, &exception);

    if (exception) {
        printf("[payload] reinject: Loader.Init() threw an exception\n");
        return false;
    }

    printf("[payload] reinject: successfully re-created overlay\n");
    return true;
}

// ---------------------------------------------------------------------------
// payload::shutdown
// ---------------------------------------------------------------------------

void shutdown()
{
    s_loader_init = nullptr;
    s_heartbeat.store(0, std::memory_order_relaxed);
}

} // namespace payload
