#pragma once

#include <Windows.h>
#include <string>

namespace sm3spectacular {

enum class VisualQuality : int {
    ColorOnly = 0,
    Balanced = 1,
    Cinema = 2,
};

// High-level remaster bundle. Custom = honor individual keys as written.
enum class RemasterPreset : int {
    Custom = 0,
    Performance = 1,
    Balanced = 2,
    Ultra = 3,
};

struct Config {
    int fpsLimit = 60;
    // Native OSD only — ignored by hybrid companion (use ReShade overlay FPS).
    bool showFpsCounter = false;
    // Native postfx (AO/SSR/grade). Off when UseReShade=1 (Remastered visuals).
    bool visuals = false;
    float visualStrength = 0.85f;
    VisualQuality quality = VisualQuality::Cinema;
    RemasterPreset preset = RemasterPreset::Custom;

    // Hybrid: chain TeaserPlay Remastered ReShade as d3d9_reshade.dll.
    bool useReShade = true;

    float depthNear = 0.15f;
    float depthFar = 4000.f;
    bool depthReverse = false;
    bool depthFlip = false;

    // Post AA — off by default (softens the image). Prefer sharp + AF/CAS.
    bool fxaa = false;
    // Neon bloom — sharp extract (only hot neon/lamps). Off in Performance.
    bool bloom = true;
    // Remastered-style AmbientLight (threshold glow + mild dirt).
    bool ambientLight = true;
    // BloomingHDR-style auto exposure.
    bool eyeAdapt = true;
    // Finish-pass deband (flat gradients).
    bool deband = true;
    float clarity = 0.28f;
    float unsharp = 0.33f;
    float aoAmount = 1.0f;
    float giAmount = 0.40f;
    float fakeHdrAmount = 0.28f;

    // Texture filtering
    bool anisotropic = true;
    int anisotropy = 16;
    // Negative = keep hi-mips longer (sharper mid-distance). Safe range ~ -1..0.
    float textureLodBias = -0.25f;
    // CAS sharpen after grade/bloom (0 = off, ~0.55 = remaster default).
    float casSharpen = 0.55f;

    // Safe building LOD / city detail (no shadow kills)
    bool lodBoost = true;
    float lodHighDistance = 400.f;
    float lodMediumDistance = 900.f;
    float lodInteriorDistance = 50.f;
    float lodDistrictCull = 0.f;
    int cityDetail = 2;
    float lodCityFar = 40000.f;
    float lodCityNear = 10.f;
    int lodCityBudget = 400000;
    int lodCityBudgetC = 80000;

    // Camera — vanilla FIELD_OF_VIEW ≈ 67
    int fovDegrees = 75;
    // Widen FOV on ultrawide relative to 16:9.
    bool aspectFovCorrect = true;

    // Display — 0 = leave game's resolution. DesktopRes=1 uses primary monitor size.
    bool desktopResolution = false;
    int displayWidth = 0;
    int displayHeight = 0;

    // F8 (default) toggles 30 FPS mission-safe ↔ preferred limit.
    int fpsToggleVk = 0x77;  // VK_F8

    // Atmosphere / particles (GRAPHOPTS)
    bool fogDepth = true;
    bool fogVolumetric = true;  // cinematic air; set FogVolumetric=0 for clear skyline
    float particleScale = 3600.f;  // vanilla 2400

    // Street life
    bool cityLife = true;
    float trafficDensity = 2.0f;
    int trafficMaxVehicles = 96;
    int trafficMaxLods = 1500;
    float trafficSpawnMedium = 280.f;
    float trafficSpawnFar = 420.f;
    float trafficSpawnVeryFar = 1000.f;
    float trafficLevelMedium = 140.f;
    float trafficLevelFar = 480.f;
    float trafficLevelVeryFar = 1000.f;
    float trafficVehicleSpeed = 13.f;
    float trafficCopSpeed = 32.f;
    float trafficThugSpeed = 26.f;
    bool trafficPromoteHero = true;
    int pedsMaxVisible = -1;
    int maxFakePeds = 200;
    int teamMaxTokens = 10;
    float entitiesFadeIn = 280.f;
    float entitiesFadeOut = 400.f;
    float entitiesNoFadeRadius = 180.f;

    // 0=off, 1=auto (default), 2=force Steam-safe visuals always
    int steamCompat = 1;
    // Exclusive fullscreen + Steam overlay breaks depth; prefer windowed under Steam.
    bool steamForceWindowed = true;

    std::wstring chainDll = L"d3d9_reshade.dll";
};

Config& config();
void loadConfig();
void saveFpsLimitToIni();
std::wstring moduleDirectory();

}  // namespace sm3spectacular
