// tools/injector.cpp
// FireRush-Internal Manual Map Injector (x86)
// Adapted from SSJJ-Internal. Maps firerush_overlay.dll into the target
// process without LoadLibrary — the DLL never appears in PEB module lists.
// Usage: injector.exe [process_name] [dll_name] [--loadlibrary]

#include <Windows.h>
#include <TlHelp32.h>
#include <cstdio>
#include <cstring>
#include <vector>

// --- Types ---

using NTSTATUS = LONG;

using fn_NtCreateThreadEx = NTSTATUS(NTAPI*)(
    PHANDLE hThread, ACCESS_MASK access, PVOID objAttr,
    HANDLE hProcess, PVOID startRoutine, PVOID argument,
    ULONG flags, SIZE_T zeroBits, SIZE_T stackSize,
    SIZE_T maxStackSize, PVOID attrList);

using fn_DllMain = BOOL(WINAPI*)(HMODULE, DWORD, LPVOID);

// Passed to shellcode running inside the target process.
// All function pointers come from kernel32 which shares the same base
// address across all processes on a given boot.
struct ManualMapData {
    BYTE*   image_base;
    HMODULE (WINAPI* pLoadLibraryA)(LPCSTR);
    FARPROC (WINAPI* pGetProcAddress)(HMODULE, LPCSTR);
    DWORD   result; // 0=pending, 1=success, 0xF0+=error
};

// =============================================================================
// Shellcode — position-independent code that runs inside the target process.
// MUST NOT reference any global/static data, string literals, or CRT functions.
// =============================================================================

#pragma runtime_checks("", off)
#pragma optimize("", off)

static DWORD WINAPI shellcode_stub(ManualMapData* data) {
    if (!data)
        return 0;

    BYTE* base = data->image_base;
    if (!base) {
        data->result = 0xF0;
        return 0;
    }

    auto dos = (IMAGE_DOS_HEADER*)base;
    auto nt  = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    auto& opt = nt->OptionalHeader;

    // ---- Resolve imports ----
    if (opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size) {
        auto desc = (IMAGE_IMPORT_DESCRIPTOR*)(
            base + opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

        while (desc->Name) {
            char* mod_name = (char*)(base + desc->Name);
            HMODULE hMod = data->pLoadLibraryA(mod_name);
            if (!hMod) {
                data->result = 0xF1;
                return 0;
            }

            auto thunk = (IMAGE_THUNK_DATA*)(base +
                (desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk));
            auto iat = (IMAGE_THUNK_DATA*)(base + desc->FirstThunk);

            for (; thunk->u1.AddressOfData; ++thunk, ++iat) {
                if (IMAGE_SNAP_BY_ORDINAL(thunk->u1.Ordinal)) {
                    iat->u1.Function = (ULONG_PTR)data->pGetProcAddress(
                        hMod, (LPCSTR)(thunk->u1.Ordinal & 0xFFFF));
                } else {
                    auto ibn = (IMAGE_IMPORT_BY_NAME*)(base + thunk->u1.AddressOfData);
                    iat->u1.Function = (ULONG_PTR)data->pGetProcAddress(hMod, ibn->Name);
                }
            }
            ++desc;
        }
    }

    // ---- Execute TLS callbacks ----
    if (opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].Size) {
        auto tls = (IMAGE_TLS_DIRECTORY*)(
            base + opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].VirtualAddress);
        auto callbacks = (PIMAGE_TLS_CALLBACK*)(tls->AddressOfCallBacks);
        if (callbacks) {
            while (*callbacks) {
                (*callbacks)((PVOID)base, DLL_PROCESS_ATTACH, nullptr);
                ++callbacks;
            }
        }
    }

    // ---- Call entry point (_DllMainCRTStartup -> CRT init -> DllMain) ----
    if (opt.AddressOfEntryPoint) {
        auto entry = (fn_DllMain)(base + opt.AddressOfEntryPoint);
        entry((HMODULE)base, DLL_PROCESS_ATTACH, nullptr);
    }

    data->result = 1;
    return 0;
}

// Marker function placed immediately after shellcode for size calculation.
static void shellcode_stub_end() { }

#pragma optimize("", on)
#pragma runtime_checks("", restore)

// =============================================================================
// Utility functions
// =============================================================================

static DWORD find_process(const char* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32 pe{};
    pe.dwSize = sizeof(pe);
    for (BOOL ok = Process32First(snap, &pe); ok; ok = Process32Next(snap, &pe)) {
        if (_stricmp(pe.szExeFile, name) == 0) {
            CloseHandle(snap);
            return pe.th32ProcessID;
        }
    }
    CloseHandle(snap);
    return 0;
}

static bool read_dll(const char* path, std::vector<BYTE>& out) {
    HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, 0, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD size = GetFileSize(hFile, nullptr);
    if (size == INVALID_FILE_SIZE || size < sizeof(IMAGE_DOS_HEADER)) {
        CloseHandle(hFile);
        return false;
    }

    out.resize(size);
    DWORD bytesRead = 0;
    BOOL ok = ReadFile(hFile, out.data(), size, &bytesRead, nullptr);
    CloseHandle(hFile);
    if (!ok || bytesRead != size) return false;

    // Validate PE
    auto dos = (IMAGE_DOS_HEADER*)out.data();
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        printf("[!] Invalid DOS signature\n");
        return false;
    }

    auto nt = (IMAGE_NT_HEADERS*)(out.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        printf("[!] Invalid NT signature\n");
        return false;
    }
    if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386) {
        printf("[!] Not an x86 DLL (machine: 0x%X)\n", nt->FileHeader.Machine);
        return false;
    }
    if (nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        printf("[!] Invalid optional header magic\n");
        return false;
    }

    return true;
}

// =============================================================================
// Manual mapping core (x86)
// =============================================================================

static bool manual_map(HANDLE hProc, const std::vector<BYTE>& raw) {
    auto dos = (const IMAGE_DOS_HEADER*)raw.data();
    auto nt  = (const IMAGE_NT_HEADERS*)(raw.data() + dos->e_lfanew);
    auto& opt = nt->OptionalHeader;

    // 1. Allocate image memory in target
    BYTE* remote_base = (BYTE*)VirtualAllocEx(hProc, nullptr, opt.SizeOfImage,
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!remote_base) {
        printf("[!] VirtualAllocEx failed for image (%u bytes): %lu\n",
               opt.SizeOfImage, GetLastError());
        return false;
    }
    printf("[+] Remote image: 0x%08X (%u bytes)\n", (DWORD)(uintptr_t)remote_base, opt.SizeOfImage);

    // 2. Build mapped image locally (headers + sections)
    std::vector<BYTE> mapped(opt.SizeOfImage, 0);
    memcpy(mapped.data(), raw.data(), opt.SizeOfHeaders);

    auto section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (section->SizeOfRawData == 0) continue;
        memcpy(mapped.data() + section->VirtualAddress,
               raw.data() + section->PointerToRawData,
               section->SizeOfRawData);
    }

    // 3. Apply base relocations (x86: HIGHLOW only)
    uintptr_t delta = (uintptr_t)remote_base - opt.ImageBase;
    if (delta) {
        auto& reloc_dir = opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        if (!reloc_dir.Size) {
            printf("[!] No relocations but rebasing is required\n");
            VirtualFreeEx(hProc, remote_base, 0, MEM_RELEASE);
            return false;
        }

        auto reloc = (IMAGE_BASE_RELOCATION*)(mapped.data() + reloc_dir.VirtualAddress);
        BYTE* reloc_end = (BYTE*)reloc + reloc_dir.Size;

        while ((BYTE*)reloc < reloc_end && reloc->SizeOfBlock) {
            DWORD count = (reloc->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
            WORD* entries = (WORD*)(reloc + 1);

            for (DWORD j = 0; j < count; ++j) {
                WORD type   = entries[j] >> 12;
                WORD offset = entries[j] & 0xFFF;

                if (type == IMAGE_REL_BASED_HIGHLOW) {
                    *(DWORD*)(mapped.data() + reloc->VirtualAddress + offset) += (DWORD)delta;
                }
                // IMAGE_REL_BASED_ABSOLUTE = padding, skip
            }

            reloc = (IMAGE_BASE_RELOCATION*)((BYTE*)reloc + reloc->SizeOfBlock);
        }
        printf("[+] Relocations applied (delta: 0x%08X)\n", (DWORD)delta);
    }

    // 4. Write mapped image to target process
    if (!WriteProcessMemory(hProc, remote_base, mapped.data(), opt.SizeOfImage, nullptr)) {
        printf("[!] WriteProcessMemory failed: %lu\n", GetLastError());
        VirtualFreeEx(hProc, remote_base, 0, MEM_RELEASE);
        return false;
    }
    printf("[+] Image written to target\n");

    // 5. Prepare shellcode region (code + ManualMapData struct)
    size_t sc_size = (BYTE*)shellcode_stub_end - (BYTE*)shellcode_stub;
    if (sc_size == 0 || sc_size > 0x2000) {
        printf("[*] Shellcode size heuristic failed (%zu), using 4KB fallback\n", sc_size);
        sc_size = 0x1000;
    }

    size_t data_offset = (sc_size + 15) & ~(size_t)15; // 16-byte align data
    size_t alloc_size  = data_offset + sizeof(ManualMapData);

    BYTE* remote_shell = (BYTE*)VirtualAllocEx(hProc, nullptr, alloc_size,
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!remote_shell) {
        printf("[!] VirtualAllocEx failed for shellcode: %lu\n", GetLastError());
        VirtualFreeEx(hProc, remote_base, 0, MEM_RELEASE);
        return false;
    }

    BYTE* remote_data_ptr = remote_shell + data_offset;

    // Fill ManualMapData with function pointers from kernel32
    ManualMapData map_data{};
    map_data.image_base      = remote_base;
    map_data.pLoadLibraryA   = (decltype(map_data.pLoadLibraryA))
        GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
    map_data.pGetProcAddress = (decltype(map_data.pGetProcAddress))
        GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetProcAddress");
    map_data.result = 0;

    // Write shellcode bytes + data struct
    WriteProcessMemory(hProc, remote_shell, shellcode_stub, sc_size, nullptr);
    WriteProcessMemory(hProc, remote_data_ptr, &map_data, sizeof(map_data), nullptr);

    printf("[+] Shellcode: 0x%08X (%zu bytes), data: 0x%08X\n",
           (DWORD)(uintptr_t)remote_shell, sc_size, (DWORD)(uintptr_t)remote_data_ptr);

    // 6. Execute shellcode — try NtCreateThreadEx first, fallback to CreateRemoteThread
    HANDLE hThread = nullptr;

    auto pNtCreateThreadEx = (fn_NtCreateThreadEx)
        GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtCreateThreadEx");

    if (pNtCreateThreadEx) {
        NTSTATUS status = pNtCreateThreadEx(
            &hThread, THREAD_ALL_ACCESS, nullptr,
            hProc, remote_shell, remote_data_ptr,
            0, 0, 0, 0, nullptr);
        if (status < 0 || !hThread) {
            printf("[*] NtCreateThreadEx: 0x%lX, falling back to CreateRemoteThread\n", status);
            hThread = nullptr;
        }
    }

    if (!hThread) {
        hThread = CreateRemoteThread(hProc, nullptr, 0,
            (LPTHREAD_START_ROUTINE)remote_shell, remote_data_ptr, 0, nullptr);
    }

    if (!hThread) {
        printf("[!] Failed to create remote thread: %lu\n", GetLastError());
        VirtualFreeEx(hProc, remote_shell, 0, MEM_RELEASE);
        VirtualFreeEx(hProc, remote_base, 0, MEM_RELEASE);
        return false;
    }

    // 7. Wait for shellcode to finish
    printf("[*] Waiting for shellcode...\n");
    DWORD wait = WaitForSingleObject(hThread, 30000);
    CloseHandle(hThread);

    if (wait == WAIT_TIMEOUT) {
        printf("[!] Shellcode timed out (30s)\n");
        return false;
    }

    // 8. Read result and free shellcode allocation (image stays mapped)
    ManualMapData result_data{};
    ReadProcessMemory(hProc, remote_data_ptr, &result_data, sizeof(result_data), nullptr);
    VirtualFreeEx(hProc, remote_shell, 0, MEM_RELEASE);

    if (result_data.result == 1) {
        printf("[+] Manual map succeeded! Base: 0x%08X\n", (DWORD)(uintptr_t)remote_base);
        return true;
    }

    printf("[!] Shellcode error: 0x%lX\n", result_data.result);
    VirtualFreeEx(hProc, remote_base, 0, MEM_RELEASE);
    return false;
}

// =============================================================================
// Classic LoadLibraryA injection (fallback mode)
// =============================================================================

static bool inject_loadlibrary(DWORD pid, const char* dll_path) {
    HANDLE proc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!proc) {
        printf("[!] Failed to open process (error: %lu)\n", GetLastError());
        return false;
    }

    size_t path_len = strlen(dll_path) + 1;
    void* remote_mem = VirtualAllocEx(proc, nullptr, path_len,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote_mem) {
        printf("[!] Failed to allocate remote memory\n");
        CloseHandle(proc);
        return false;
    }

    WriteProcessMemory(proc, remote_mem, dll_path, path_len, nullptr);

    HANDLE thread = CreateRemoteThread(proc, nullptr, 0,
        (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA"),
        remote_mem, 0, nullptr);

    if (!thread) {
        printf("[!] Failed to create remote thread (error: %lu)\n", GetLastError());
        VirtualFreeEx(proc, remote_mem, 0, MEM_RELEASE);
        CloseHandle(proc);
        return false;
    }

    WaitForSingleObject(thread, INFINITE);

    DWORD exit_code = 0;
    GetExitCodeThread(thread, &exit_code);

    CloseHandle(thread);
    VirtualFreeEx(proc, remote_mem, 0, MEM_RELEASE);
    CloseHandle(proc);

    printf("[+] LoadLibrary injection %s (module handle: 0x%lX)\n",
           exit_code ? "succeeded" : "FAILED", exit_code);
    return exit_code != 0;
}

// =============================================================================
// Entry point
// =============================================================================

int main(int argc, char* argv[]) {
    printf("=== FireRush-Internal Injector (x86) ===\n\n");

    const char* process_name = "firerush.exe";
    const char* dll_name = "firerush_overlay.dll";
    bool use_loadlibrary = false;

    for (int i = 1; i < argc; ++i) {
        if (_stricmp(argv[i], "--loadlibrary") == 0 || _stricmp(argv[i], "-l") == 0) {
            use_loadlibrary = true;
        } else if (i == 1) {
            process_name = argv[i];
        } else if (i == 2) {
            dll_name = argv[i];
        }
    }

    char dll_path[MAX_PATH];
    GetFullPathNameA(dll_name, MAX_PATH, dll_path, nullptr);

    printf("[*] Target:  %s\n", process_name);
    printf("[*] DLL:     %s\n", dll_path);
    printf("[*] Method:  %s\n\n", use_loadlibrary ? "LoadLibrary" : "Manual Map");

    if (use_loadlibrary) {
        // --- LoadLibrary path (simple, visible in module list) ---
        if (GetFileAttributesA(dll_path) == INVALID_FILE_ATTRIBUTES) {
            printf("[!] DLL file not found: %s\n", dll_path);
            return 1;
        }

        printf("[*] Waiting for %s...\n", process_name);
        DWORD pid = 0;
        while (!(pid = find_process(process_name))) {
            Sleep(500);
        }
        printf("[+] Found PID: %lu\n", pid);

        Sleep(3000);
        printf("[*] Injecting via LoadLibrary...\n");

        if (inject_loadlibrary(pid, dll_path)) {
            printf("[+] Done! Press Insert to toggle menu, End to unload.\n");
        } else {
            printf("[!] Injection failed.\n");
            return 1;
        }
    } else {
        // --- Manual Map path (hidden from PEB module list) ---
        std::vector<BYTE> dll_raw;
        if (!read_dll(dll_path, dll_raw)) {
            printf("[!] Failed to read or validate DLL: %s\n", dll_path);
            return 1;
        }
        printf("[+] DLL loaded (%zu bytes)\n\n", dll_raw.size());

        printf("[*] Waiting for %s...\n", process_name);
        DWORD pid = 0;
        while (!(pid = find_process(process_name))) {
            Sleep(500);
        }
        printf("[+] Found PID: %lu\n", pid);

        Sleep(3000);

        HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
        if (!hProc) {
            printf("[!] OpenProcess failed: %lu\n", GetLastError());
            return 1;
        }

        printf("[*] Mapping DLL...\n");
        bool ok = manual_map(hProc, dll_raw);
        CloseHandle(hProc);

        if (ok) {
            printf("[+] Done! Press Insert to toggle menu, End to unload.\n");
        } else {
            printf("[!] Manual map injection failed.\n");
            printf("[*] Tip: try --loadlibrary flag as fallback.\n");
            return 1;
        }
    }

    return 0;
}
