// binkw32.dll proxy — Game.exe always loads local binkw32 (winmm stays System32).
#include "companion.hpp"

#include <Windows.h>

namespace {

// Never do heavy work under DllMain loader lock (deadlocks with ReShade/d3d9).
DWORD WINAPI CompanionBootstrap(LPVOID) {
    // Yield so DllMain can return and release the loader lock, then init ASAP
    // so Direct3DCreate9 IAT hook lands before the game's first Create9.
    SwitchToThread();
    sm3spectacular::companionInit();
    return 0;
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinst);
        HANDLE t = CreateThread(nullptr, 0, CompanionBootstrap, nullptr, 0, nullptr);
        if (t) {
            CloseHandle(t);
        }
    }
    return TRUE;
}
