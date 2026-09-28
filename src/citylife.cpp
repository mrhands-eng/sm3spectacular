#include "citylife.hpp"

#include "config.hpp"
#include "exeid.hpp"
#include "log.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace sm3spectacular {
namespace {

// Graphopts / debug-option value slots (verified against Game.exe option table).
constexpr uintptr_t kEnablePeds = 0x00D0C72A;           // byte
constexpr uintptr_t kPedsMaxVisible = 0x00D0C72C;       // int (-1 = unlimited)
constexpr uintptr_t kEnableFakePeds = 0x00D0C730;       // byte
constexpr uintptr_t kMaxFakePeds = 0x00D0C734;          // int (-1 = unlimited)
constexpr uintptr_t kEnableTraffic = 0x00D0C738;        // byte
constexpr uintptr_t kTrafficDensity = 0x00D0C73C;       // float vanilla 0.5
constexpr uintptr_t kTrafficMaxVehicles = 0x00D0C740;   // int vanilla 30
constexpr uintptr_t kTrafficMaxLods = 0x00D0C744;       // int vanilla 480
constexpr uintptr_t kTrafficSpawnMed = 0x00D0C748;      // float 120
constexpr uintptr_t kTrafficSpawnFar = 0x00D0C74C;      // float 170
constexpr uintptr_t kTrafficSpawnVFar = 0x00D0C750;     // float 500
constexpr uintptr_t kTrafficLevelMed = 0x00D0C754;      // float 50
constexpr uintptr_t kTrafficLevelFar = 0x00D0C758;      // float 200
constexpr uintptr_t kTrafficLevelVFar = 0x00D0C75C;     // float 500
constexpr uintptr_t kTrafficDrive = 0x00D0C760;         // byte
constexpr uintptr_t kTrafficLevelize = 0x00D0C761;      // byte
constexpr uintptr_t kTrafficVehSpeed = 0x00D0C764;      // float 10
constexpr uintptr_t kTrafficCopSpeed = 0x00D0C768;      // float 25
constexpr uintptr_t kTrafficThugSpeed = 0x00D0C76C;     // float 20
constexpr uintptr_t kTrafficPromoteHero = 0x00DEACE1;   // byte

constexpr uintptr_t kEntitiesFadeIn = 0x00D0C824;       // float 40
constexpr uintptr_t kEntitiesFadeOut = 0x00D0C828;      // float 50
constexpr uintptr_t kEntitiesNoFade = 0x00D0C82C;       // float 20

constexpr uintptr_t kTeamSpiderman = 0x00D0C894;
constexpr uintptr_t kTeamPolice = 0x00D0C898;
constexpr uintptr_t kTeamCivilian = 0x00D0C89C;
constexpr uintptr_t kTeamBoss = 0x00D0C8A0;
constexpr uintptr_t kTeamGangA = 0x00D0C8A4;
constexpr uintptr_t kTeamGangB = 0x00D0C8A8;
constexpr uintptr_t kTeamGangC = 0x00D0C8AC;
constexpr uintptr_t kTeamGangD = 0x00D0C8B0;
constexpr uintptr_t kTeamAtomicPunk = 0x00D0C8B4;
constexpr uintptr_t kTeamPanAsian = 0x00D0C8B8;
constexpr uintptr_t kTeamGothicLolita = 0x00D0C8BC;
constexpr uintptr_t kTeamOldTime = 0x00D0C8C0;
constexpr uintptr_t kTeamSpecialA = 0x00D0C8C4;
constexpr uintptr_t kTeamSpecialB = 0x00D0C8C8;

bool g_installed = false;
unsigned g_presents = 0;

bool writeMem(void* addr, const void* src, size_t n) {
    DWORD old = 0;
    if (!VirtualProtect(addr, n, PAGE_EXECUTE_READWRITE, &old)) {
        return false;
    }
    std::memcpy(addr, src, n);
    VirtualProtect(addr, n, old, &old);
    return true;
}

void writeByte(uintptr_t va, uint8_t v) { writeMem(reinterpret_cast<void*>(va), &v, 1); }
void writeInt(uintptr_t va, int v) { writeMem(reinterpret_cast<void*>(va), &v, sizeof(v)); }
void writeFloat(uintptr_t va, float v) { writeMem(reinterpret_cast<void*>(va), &v, sizeof(v)); }

template <typename T>
T readVal(uintptr_t va) {
    T v{};
    std::memcpy(&v, reinterpret_cast<const void*>(va), sizeof(T));
    return v;
}

// Traffic / peds / fade — safe to restick often (missions overwrite density).
void applyCityTraffic() {
    const auto& c = config();
    if (!c.cityLife) {
        return;
    }

    writeByte(kEnablePeds, 1);
    writeByte(kEnableFakePeds, 1);
    writeByte(kEnableTraffic, 1);
    writeByte(kTrafficDrive, 1);
    writeByte(kTrafficLevelize, 1);
    writeByte(kTrafficPromoteHero, c.trafficPromoteHero ? 1 : 0);

    writeInt(kPedsMaxVisible, c.pedsMaxVisible);
    writeInt(kMaxFakePeds, c.maxFakePeds);

    writeFloat(kTrafficDensity, c.trafficDensity);
    writeInt(kTrafficMaxVehicles, c.trafficMaxVehicles);
    writeInt(kTrafficMaxLods, c.trafficMaxLods);

    writeFloat(kTrafficSpawnMed, c.trafficSpawnMedium);
    writeFloat(kTrafficSpawnFar, c.trafficSpawnFar);
    writeFloat(kTrafficSpawnVFar, c.trafficSpawnVeryFar);
    writeFloat(kTrafficLevelMed, c.trafficLevelMedium);
    writeFloat(kTrafficLevelFar, c.trafficLevelFar);
    writeFloat(kTrafficLevelVFar, c.trafficLevelVeryFar);

    writeFloat(kTrafficVehSpeed, c.trafficVehicleSpeed);
    writeFloat(kTrafficCopSpeed, c.trafficCopSpeed);
    writeFloat(kTrafficThugSpeed, c.trafficThugSpeed);

    writeFloat(kEntitiesFadeIn, c.entitiesFadeIn);
    writeFloat(kEntitiesFadeOut, c.entitiesFadeOut);
    writeFloat(kEntitiesNoFade, c.entitiesNoFadeRadius);
}

// Fight tokens — apply rarely so missions can temporarily lower AI presence.
void applyCityTokens() {
    const auto& c = config();
    if (!c.cityLife) {
        return;
    }
    const int tok = c.teamMaxTokens;
    writeInt(kTeamCivilian, tok);
    writeInt(kTeamPolice, tok);
    writeInt(kTeamBoss, (std::max)(tok, 5));
    writeInt(kTeamGangA, tok);
    writeInt(kTeamGangB, tok);
    writeInt(kTeamGangC, tok);
    writeInt(kTeamGangD, tok);
    writeInt(kTeamAtomicPunk, tok);
    writeInt(kTeamPanAsian, tok);
    writeInt(kTeamGothicLolita, tok);
    writeInt(kTeamOldTime, tok);
    writeInt(kTeamSpecialA, tok);
    writeInt(kTeamSpecialB, tok);
    writeInt(kTeamSpiderman, tok);
}

void verifyCityLifeOnce() {
    static bool done = false;
    if (done) {
        return;
    }
    done = true;
    const float density = readVal<float>(kTrafficDensity);
    const int maxVeh = readVal<int>(kTrafficMaxVehicles);
    const int maxFake = readVal<int>(kMaxFakePeds);
    const int pedsVis = readVal<int>(kPedsMaxVisible);
    const int tokens = readVal<int>(kTeamCivilian);
    const uint8_t pedsOn = readVal<uint8_t>(kEnablePeds);
    const uint8_t trafficOn = readVal<uint8_t>(kEnableTraffic);
    const uint8_t promote = readVal<uint8_t>(kTrafficPromoteHero);
    const float fadeIn = readVal<float>(kEntitiesFadeIn);
    logf("citylife.verify dens=%.2f maxVeh=%d maxFake=%d pedsVis=%d tokens=%d "
         "enablePeds=%u enableTraffic=%u promote=%u fadeIn=%.0f",
         density, maxVeh, maxFake, pedsVis, tokens, (unsigned)pedsOn, (unsigned)trafficOn,
         (unsigned)promote, fadeIn);

    const auto& c = config();
    if (std::fabs(density - c.trafficDensity) > 0.01f || maxVeh != c.trafficMaxVehicles ||
        maxFake != c.maxFakePeds || tokens != c.teamMaxTokens || !pedsOn || !trafficOn) {
        logf("citylife.verify WARNING: readback mismatch (mission overwrite or bad VA?)");
    }
}

}  // namespace

void citylifeInstall() {
    if (g_installed) {
        return;
    }
    g_installed = true;
    if (!exeIdentityOk()) {
        logf("citylife: skipped (exe identity mismatch)");
        return;
    }
    if (!config().cityLife) {
        logf("citylife: OFF (vanilla traffic/peds)");
        return;
    }
    applyCityTraffic();
    applyCityTokens();
    logf("citylife: ON density=%.2f maxVeh=%d maxFake=%d tokens=%d fadeIn=%.0f",
         config().trafficDensity, config().trafficMaxVehicles, config().maxFakePeds,
         config().teamMaxTokens, config().entitiesFadeIn);
}

void citylifeOnPresent() {
    if (!exeIdentityOk() || !config().cityLife || !g_installed) {
        return;
    }
    ++g_presents;
    // Density/spawn restick — missions call set_traffic_density often.
    if ((g_presents % 60) == 1) {
        applyCityTraffic();
        if (g_presents >= 90) {
            verifyCityLifeOnce();
        }
    }
    // Tokens rarely — avoid fighting mission AI budgets every second.
    if ((g_presents % 600) == 1) {
        applyCityTokens();
    }
}

}  // namespace sm3spectacular
