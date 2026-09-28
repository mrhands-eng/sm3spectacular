#include "menu.hpp"

#include "config.hpp"
#include "fps.hpp"
#include "log.hpp"

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>

namespace sm3spectacular {
namespace {

// AutoExp left/right bodies (after `cmp eax,4; jne`), through `jmp continue` = 20 bytes.
constexpr uintptr_t kLeftBodyVa = 0x6F8D7D;
constexpr uintptr_t kRightBodyVa = 0x6F0068;
constexpr uintptr_t kLeftContinueVa = 0x6F8E53;
constexpr uintptr_t kRightContinueVa = 0x6F0162;

// AutoExp value-label select. NOTE: 0x6A62C0 is the last byte of the preceding
// `jz` that skips this block when the value widget is null. Real entry is 0x6A62C1.
// ON-path `jmp short` lands on 0x6A631B (`mov [esp+0x18], ebx`), NOT 0x6A631A
// (`push edx` — last insn of the OFF path).
constexpr uintptr_t kLabelSelectVa = 0x6A62C1;
constexpr uintptr_t kLabelMergeVa = 0x6A631B;
constexpr uintptr_t kStringCtorVa = 0x4010A0;

constexpr size_t kPendingAutoExpOff = 0x6D9;
constexpr size_t kBodyPatchBytes = 20;

std::atomic<int> g_pendingFps{60};
bool g_menuHooksInstalled = false;

const wchar_t* g_labelKey = nullptr;
uint32_t g_labelId = 0x1D;

// Locale keys (values in pcinterface/igct_*.bnx — any language SKU).
// Companion/native ensureIgctFpsLabels() inserts these if missing.
// StringCtor only copies the key; lookup happens later. Unknown keys can crash.
const wchar_t kLabel30[] = L"BX_3DMENU_VIDEO_OPTIONS_FPS_30";
const wchar_t kLabel60[] = L"BX_3DMENU_VIDEO_OPTIONS_FPS_60";
const wchar_t kLabel120[] = L"BX_3DMENU_VIDEO_OPTIONS_FPS_120";
const wchar_t kLabel144[] = L"BX_3DMENU_VIDEO_OPTIONS_FPS_144";
const wchar_t kLabelUnlimited[] = L"BX_3DMENU_VIDEO_OPTIONS_FPS_UNLIMITED";

bool writeMemory(void* addr, const void* src, size_t size) {
    DWORD old = 0;
    if (!VirtualProtect(addr, size, PAGE_EXECUTE_READWRITE, &old)) {
        return false;
    }
    memcpy(addr, src, size);
    VirtualProtect(addr, size, old, &old);
    FlushInstructionCache(GetCurrentProcess(), addr, size);
    return true;
}

int nextFps(int fps, int dir) {
    static const int kOrder[] = {30, 60, 120, 144, 0};
    constexpr int kCount = 5;
    int idx = 1;
    for (int i = 0; i < kCount; ++i) {
        if (kOrder[i] == fps) {
            idx = i;
            break;
        }
    }
    return kOrder[(idx + dir + kCount) % kCount];
}

extern "C" void pickFpsLabel() {
    switch (g_pendingFps.load()) {
        case 30:
            g_labelKey = kLabel30;
            g_labelId = 0x1D;
            break;
        case 120:
            g_labelKey = kLabel120;
            g_labelId = 0x1D;
            break;
        case 144:
            g_labelKey = kLabel144;
            g_labelId = 0x1D;
            break;
        case 0:
            g_labelKey = kLabelUnlimited;
            g_labelId = 0x1E;
            break;
        default:
            g_labelKey = kLabel60;
            g_labelId = 0x1D;
            break;
    }
}

extern "C" void cycleFps(int dir, void* panel) {
    const int next = nextFps(g_pendingFps.load(), dir);
    g_pendingFps.store(next);
    applyFpsLimit(next);
    saveFpsLimitToIni();

    if (panel) {
        auto* base = reinterpret_cast<uint8_t*>(panel);
        base[kPendingAutoExpOff] = (next == 30) ? 0 : 1;
    }

    logf("FPS menu cycle -> %d", next);
}

void patchAutoExpBody(uintptr_t bodyVa, uintptr_t continueVa, int dir) {
    uint8_t* cave = reinterpret_cast<uint8_t*>(
        VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!cave) {
        return;
    }

    uint8_t* p = cave;
    *p++ = 0x60;
    *p++ = 0x56;
    *p++ = 0x6A;
    *p++ = static_cast<uint8_t>(dir & 0xFF);
    *p++ = 0xE8;
    *reinterpret_cast<int32_t*>(p) = static_cast<int32_t>(
        reinterpret_cast<uintptr_t>(&cycleFps) - (reinterpret_cast<uintptr_t>(p) + 4));
    p += 4;
    *p++ = 0x83;
    *p++ = 0xC4;
    *p++ = 0x08;
    *p++ = 0x61;
    *p++ = 0xE9;
    *reinterpret_cast<int32_t*>(p) =
        static_cast<int32_t>(continueVa - (reinterpret_cast<uintptr_t>(p) + 4));

    uint8_t block[kBodyPatchBytes];
    memset(block, 0x90, sizeof(block));
    block[0] = 0xE9;
    *reinterpret_cast<int32_t*>(block + 1) =
        static_cast<int32_t>(reinterpret_cast<uintptr_t>(cave) - (bodyVa + 5));
    writeMemory(reinterpret_cast<void*>(bodyVa), block, sizeof(block));
}

void patchLabelSelect() {
    uint8_t* cave = reinterpret_cast<uint8_t*>(
        VirtualAlloc(nullptr, 128, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!cave) {
        return;
    }

    // Same stack/register contract as stock ON path @ 0x6A62CE.
    uint8_t* p = cave;
    *p++ = 0x60;  // pushad
    *p++ = 0xE8;
    *reinterpret_cast<int32_t*>(p) = static_cast<int32_t>(
        reinterpret_cast<uintptr_t>(&pickFpsLabel) - (reinterpret_cast<uintptr_t>(p) + 4));
    p += 4;
    *p++ = 0x61;  // popad

    *p++ = 0x8B;  // mov edi, [g_labelId]
    *p++ = 0x3D;
    *reinterpret_cast<uint32_t*>(p) = reinterpret_cast<uint32_t>(&g_labelId);
    p += 4;

    *p++ = 0xFF;  // push dword [g_labelKey]
    *p++ = 0x35;
    *reinterpret_cast<uint32_t*>(p) = reinterpret_cast<uint32_t>(&g_labelKey);
    p += 4;

    *p++ = 0x8D;  // lea ecx, [esp+0x8D8]
    *p++ = 0x8C;
    *p++ = 0x24;
    *reinterpret_cast<uint32_t*>(p) = 0x8D8;
    p += 4;

    *p++ = 0xE8;  // call StringCtor (stdcall, ret 4)
    *reinterpret_cast<int32_t*>(p) =
        static_cast<int32_t>(kStringCtorVa - (reinterpret_cast<uintptr_t>(p) + 4));
    p += 4;

    *p++ = 0x8D;  // lea ecx, [esp+0x8D4]
    *p++ = 0x8C;
    *p++ = 0x24;
    *reinterpret_cast<uint32_t*>(p) = 0x8D4;
    p += 4;

    *p++ = 0x89;  // mov [esp+0x17DC], edi
    *p++ = 0xBC;
    *p++ = 0x24;
    *reinterpret_cast<uint32_t*>(p) = 0x17DC;
    p += 4;

    *p++ = 0x83;  // or ebx, 0x10
    *p++ = 0xCB;
    *p++ = 0x10;

    *p++ = 0x51;  // push ecx

    *p++ = 0xE9;  // jmp merge
    *reinterpret_cast<int32_t*>(p) =
        static_cast<int32_t>(kLabelMergeVa - (reinterpret_cast<uintptr_t>(p) + 4));

    uint8_t jmp[5];
    jmp[0] = 0xE9;
    *reinterpret_cast<int32_t*>(jmp + 1) =
        static_cast<int32_t>(reinterpret_cast<uintptr_t>(cave) - (kLabelSelectVa + 5));
    writeMemory(reinterpret_cast<void*>(kLabelSelectVa), jmp, sizeof(jmp));

    logf("FPS label select hooked at 0x%X (30/60/120/144/Unlimited)", (unsigned)kLabelSelectVa);
}

}  // namespace

void setPendingFpsLimit(int fps) { g_pendingFps.store(fps); }

int pendingFpsLimit() { return g_pendingFps.load(); }

void installMenuHooks() {
    if (g_menuHooksInstalled) {
        return;
    }
    g_pendingFps.store(currentFpsLimit());

    patchAutoExpBody(kLeftBodyVa, kLeftContinueVa, +1);
    patchAutoExpBody(kRightBodyVa, kRightContinueVa, -1);
    patchLabelSelect();

    g_menuHooksInstalled = true;
    logf("SM3 Spectacular Edition by zryuyu — menu hooks installed");
}

}  // namespace sm3spectacular
