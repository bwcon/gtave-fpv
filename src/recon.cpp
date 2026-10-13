// One-shot reverse-engineering aid: logs native handler addresses and dumps the
// game's in-memory image (fpv-data/recon/gtaiv-image.bin) so the engine's line
// test can be found offline. Runs once; delete the dump to run it again.
#include "recon.h"

#include <windows.h>

#include <cstdio>

#include "log.h"
#include "scripthook.h"

namespace recon {

void run_once() {
    const char *dump_path = "fpv-data\\recon\\gtaiv-image.bin";
    if (GetFileAttributesA(dump_path) != INVALID_FILE_ATTRIBUTES) return;
    CreateDirectoryA("fpv-data", nullptr);
    CreateDirectoryA("fpv-data\\recon", nullptr);

    auto base = reinterpret_cast<uint8_t *>(GetModuleHandleA(nullptr));
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS32 *>(base + dos->e_lfanew);
    uint32_t size = nt->OptionalHeader.SizeOfImage;
    fpvlog::write("recon: image base %p size 0x%x", static_cast<void *>(base), size);

    using GetNative = void *(__cdecl *)(uint32_t);
    auto get_native = reinterpret_cast<GetNative>(reinterpret_cast<void *>(
        GetProcAddress(GetModuleHandleA("ScriptHook.dll"), "?GetNativeAddressFromHash@Game@@SAPAXI@Z")));
    static const char *natives[] = {
        "GET_GROUND_Z_FOR_3D_COORD", "GET_CHAR_HEIGHT_ABOVE_GROUND", "HAS_CHAR_SPOTTED_CHAR",
        "HAS_CHAR_SPOTTED_CHAR_IN_FRONT", "IS_POINT_OBSCURED_BY_A_MISSION_ENTITY", "GET_SAFE_POSITION_FOR_CHAR",
        "GET_WATER_HEIGHT_NO_WAVES", "GET_PLAYER_ID",
    };
    for (const char *name : natives) {
        void *fn = get_native ? get_native(sh::joaat(name)) : nullptr;
        fpvlog::write("recon: native %s handler %p (rva 0x%x)", name, fn,
                      fn ? uint32_t(static_cast<uint8_t *>(fn) - base) : 0u);
    }

    // Copy readable pages; unreadable ones are left as zeros.
    FILE *f = std::fopen(dump_path, "wb");
    if (!f) {
        fpvlog::write("recon: cannot write %s", dump_path);
        return;
    }
    static uint8_t zeros[4096];
    for (uint32_t off = 0; off < size;) {
        MEMORY_BASIC_INFORMATION mbi;
        VirtualQuery(base + off, &mbi, sizeof mbi);
        uint32_t region_end = uint32_t(static_cast<uint8_t *>(mbi.BaseAddress) - base) + uint32_t(mbi.RegionSize);
        if (region_end > size) region_end = size;
        bool readable = mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD));
        for (; off < region_end; off += 4096) {
            uint32_t n = region_end - off < 4096 ? region_end - off : 4096;
            std::fwrite(readable ? base + off : zeros, 1, n, f);
        }
    }
    std::fclose(f);
    fpvlog::write("recon: dumped image to %s", dump_path);
}

}  // namespace recon
