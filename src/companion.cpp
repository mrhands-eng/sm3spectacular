// Companion injector: binkw32.dll while Remastered ReShade owns d3d9.dll.
#include "citylife.hpp"
#include "config.hpp"
#include "exeid.hpp"
#include "fps.hpp"
#include "igct_fps.hpp"
#include "log.hpp"
#include "menu.hpp"
#include "steam_compat.hpp"
#include "vsync.hpp"
#include "world.hpp"

#include <Windows.h>

#include <atomic>
#include <mutex>

namespace sm3spectacular {
namespace {

HANDLE g_restickThread = nullptr;
std::atomic<bool> g_restickRun{false};
std::atomic<bool> g_menuDone{false};
std::atomic<int> g_preferredFps{60};

void pollFpsToggleHotkey() {
    static bool wasDown = false;
    const int vk = config().fpsToggleVk;
    const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    if (down && !wasDown) {
        const int cur = currentFpsLimit();
        if (cur == 30) {
            const int restore = g_preferredFps.load();
            applyFpsLimit(restore);
            setPendingFpsLimit(restore);
            logf("FPS hotkey: restored %d (mission unlock)", restore);
        } else {
            g_preferredFps.store(cur <= 0 ? 0 : cur);
            applyFpsLimit(30);
            setPendingFpsLimit(30);
            logf("FPS hotkey: 30 FPS (mission-safe). Press again to restore.");
        }
    }
    wasDown = down;
}

DWORD WINAPI RestickThreadProc(LPVOID) {
    logf("companion restick thread started");
    // Hold CreateDevice outermost through ReShade init; then install menu.
    // Slow cadence — aggressive re-wrap raced CreateDevice and crashed (0xC0000005).
    for (int i = 0; i < 80; ++i) {
        installVsyncUnlock();
        Sleep(25);
    }
    Sleep(500);
    if (!g_menuDone.exchange(true) && exeIdentityOk()) {
        installMenuHooks();
        logf("companion: FPS menu hooks installed (30/60/120/144/Unlimited)");
    }
    // Overlay may load after first init — re-apply Techniques once device is up.
    bool steamRetried = false;
    while (g_restickRun.load()) {
        if (!steamRetried && vsyncCreateDeviceSeen()) {
            applySteamCompat();
            steamRetried = true;
            logf("companion: SteamCompat re-applied after CreateDevice");
        }
        pollFpsToggleHotkey();
        {
            const int cur = currentFpsLimit();
            if (cur != 30) {
                g_preferredFps.store(cur <= 0 ? 0 : cur);
            }
        }
        fpsOnTick();
        citylifeOnPresent();
        worldOnPresent();
        Sleep(16);
    }
    return 0;
}

}  // namespace

void companionInit() {
    static std::once_flag once;
    std::call_once(once, [] {
        loadConfig();
        logInit();
        logf("SM3 Spectacular companion (binkw32.dll) — UseReShade hybrid");
        logf("FPS/CityLife/LOD/FOV/menu; visuals = Remastered ReShade as d3d9.dll");
        ensureIgctFpsLabels();
        applySteamCompat();
        // Before Direct3DCreate9: patch IAT so CreateDevice can drop VSync.
        installVsyncUnlock();
        if (!exeIdentityOk()) {
            logf("companion: exe identity mismatch — VA patches skipped");
            g_restickRun.store(true);
            g_restickThread = CreateThread(nullptr, 0, RestickThreadProc, nullptr, 0, nullptr);
            return;
        }
        installFpsPatches();
        citylifeInstall();
        g_preferredFps.store(config().fpsLimit);
        g_restickRun.store(true);
        g_restickThread = CreateThread(nullptr, 0, RestickThreadProc, nullptr, 0, nullptr);
        logf("companion init OK (FpsToggleVk=0x%X fogVol=%d particles=%.0f)",
             config().fpsToggleVk, (int)config().fogVolumetric, config().particleScale);
    });
}

}  // namespace sm3spectacular
