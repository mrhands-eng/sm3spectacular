#include "fps.hpp"

#include "config.hpp"
#include "exeid.hpp"
#include "log.hpp"

#include <Windows.h>

#include <atomic>
#include <cmath>
#include <cstring>

namespace sm3spectacular {
namespace {

// Simulation / dt scaling in .rdata (vanilla ~1/30). Used by physics etc.
// Never write 0 here — world load / dt code can hang.
constexpr uintptr_t kSimPeriodVa = 0xA6C53C;

// Display frame wait in .data (vanilla bytes 88 88 08 3D ≈ 1/30).
// Main loop: comiss dt, [D0C604]; jb wait_more  @ 0x546177
// Classic hex unlock zeroes this float; that is the real Present-rate lock.
constexpr uintptr_t kDisplayPeriodVa = 0xD0C604;

// Hardcoded `push 0x3D088889` (1/30) call sites that stamp period into subsystems.
constexpr uintptr_t kPushImm30Vas[] = {0x5462F9, 0x85D491, 0x85D4A3, 0x85D4B5, 0x85D4CB};

std::atomic<int> g_fpsLimit{60};
std::atomic<int> g_presentCount{0};
std::atomic<bool> g_vaLive{false};
LARGE_INTEGER g_qpcFreq{};
LARGE_INTEGER g_lastPresent{};
bool g_qpcReady = false;

LARGE_INTEGER g_measureStart{};
int g_measureFrames = 0;

bool vaSafe() { return g_vaLive.load() && exeIdentityOk(); }

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

float displayPeriodFor(int fps) {
    if (fps > 0) {
        return 1.0f / static_cast<float>(fps);
    }
    // Unlimited: classic unlock — wait threshold 0 so the jb never loops.
    return 0.0f;
}

float simPeriodFor(int fps) {
    if (fps > 0) {
        return 1.0f / static_cast<float>(fps);
    }
    // Unlimited sim step: small non-zero (never 0).
    return 1.0f / 144.0f;
}

void patchPushImmediates(float period) {
    uint32_t bits = 0;
    memcpy(&bits, &period, sizeof(bits));
    for (uintptr_t va : kPushImm30Vas) {
        // Opcode is `68 imm32` — patch the immediate only.
        writeMemory(reinterpret_cast<void*>(va + 1), &bits, sizeof(bits));
    }
}

void patchPeriods(int fps) {
    const float display = displayPeriodFor(fps);
    const float sim = simPeriodFor(fps);
    writeMemory(reinterpret_cast<void*>(kDisplayPeriodVa), &display, sizeof(display));
    writeMemory(reinterpret_cast<void*>(kSimPeriodVa), &sim, sizeof(sim));
    patchPushImmediates(sim);
}

bool displayNeedsRepatch(int fps) {
    const float want = displayPeriodFor(fps);
    const float cur = *reinterpret_cast<float*>(kDisplayPeriodVa);
    return std::fabs(cur - want) > 1e-7f;
}

}  // namespace

void installFpsPatches() {
    QueryPerformanceFrequency(&g_qpcFreq);
    QueryPerformanceCounter(&g_lastPresent);
    g_measureStart = g_lastPresent;
    g_qpcReady = g_qpcFreq.QuadPart > 0;

    g_fpsLimit.store(config().fpsLimit);
    patchPeriods(config().fpsLimit);
    g_vaLive.store(true);
    logf("FPS patches installed FpsLimit=%d display=%.6f sim=%.6f (D0C604+A6C53C)",
         config().fpsLimit, displayPeriodFor(config().fpsLimit),
         simPeriodFor(config().fpsLimit));
}

void applyFpsLimit(int fpsLimit) {
    if (fpsLimit != 0 && fpsLimit != 30 && fpsLimit != 60 && fpsLimit != 120 &&
        fpsLimit != 144) {
        fpsLimit = 60;
    }
    g_fpsLimit.store(fpsLimit);
    config().fpsLimit = fpsLimit;
    if (vaSafe()) {
        patchPeriods(fpsLimit);
    }
    QueryPerformanceCounter(&g_lastPresent);
    logf("applyFpsLimit %d display=%.6f sim=%.6f va=%d", fpsLimit, displayPeriodFor(fpsLimit),
         simPeriodFor(fpsLimit), (int)vaSafe());
}

int currentFpsLimit() { return g_fpsLimit.load(); }

void presentFrameLimit() {
    const int n = g_presentCount.fetch_add(1) + 1;
    const int fps = g_fpsLimit.load();

    // Sticky VA restick — only on fingerprinted Game.exe after install.
    if (vaSafe() && (n % 30) == 0 && displayNeedsRepatch(fps)) {
        patchPeriods(fps);
        logf("display period re-patched (drift detected) now=%.6f",
             *reinterpret_cast<float*>(kDisplayPeriodVa));
    }

    if (g_qpcReady) {
        ++g_measureFrames;
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        const double elapsed =
            double(now.QuadPart - g_measureStart.QuadPart) / double(g_qpcFreq.QuadPart);
        if (elapsed >= 2.0) {
            const float disp =
                vaSafe() ? *reinterpret_cast<float*>(kDisplayPeriodVa) : displayPeriodFor(fps);
            logf("measured Present ~%.1f fps (limit=%d display=%.6f)", g_measureFrames / elapsed,
                 fps, disp);
            g_measureStart = now;
            g_measureFrames = 0;
        }
    }

    // Soft Present sleep only when capping (not unlimited). No VA needed.
    if (fps <= 0 || !g_qpcReady) {
        QueryPerformanceCounter(&g_lastPresent);
        return;
    }

    const LONGLONG targetTicks = g_qpcFreq.QuadPart / fps;
    if (targetTicks <= 0) {
        return;
    }

    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const LONGLONG elapsedTicks = now.QuadPart - g_lastPresent.QuadPart;
    if (elapsedTicks >= 0 && elapsedTicks < targetTicks) {
        const LONGLONG remain = targetTicks - elapsedTicks;
        const DWORD sleepMs = static_cast<DWORD>((remain * 1000) / g_qpcFreq.QuadPart);
        if (sleepMs > 1) {
            Sleep(sleepMs - 1);
        }
        do {
            QueryPerformanceCounter(&now);
        } while ((now.QuadPart - g_lastPresent.QuadPart) < targetTicks);
    }
    QueryPerformanceCounter(&g_lastPresent);
}

void fpsOnTick() {
    // Same VA restick / measure as Present path, without sleeping the game thread.
    const int n = g_presentCount.fetch_add(1) + 1;
    const int fps = g_fpsLimit.load();
    if (vaSafe() && (n % 30) == 0 && displayNeedsRepatch(fps)) {
        patchPeriods(fps);
        logf("display period re-patched (companion) now=%.6f",
             *reinterpret_cast<float*>(kDisplayPeriodVa));
    }
    if (!g_qpcReady) {
        return;
    }
    ++g_measureFrames;
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const double elapsed =
        double(now.QuadPart - g_measureStart.QuadPart) / double(g_qpcFreq.QuadPart);
    if (elapsed >= 5.0) {
        const float disp =
            vaSafe() ? *reinterpret_cast<float*>(kDisplayPeriodVa) : displayPeriodFor(fps);
        // restickHz ≈ companion loop rate (Sleep~16), not game Present FPS.
        logf("companion restick ~%.1f Hz (limit=%d displayVA=%.6f want=%.6f)",
             g_measureFrames / elapsed, fps, disp, displayPeriodFor(fps));
        g_measureStart = now;
        g_measureFrames = 0;
    }
}

}  // namespace sm3spectacular
