#include "config.hpp"

#include <mutex>
#include <string>

namespace sm3spectacular {
namespace {

Config g_config;
std::mutex g_mutex;

std::wstring getModulePath() {
    wchar_t path[MAX_PATH]{};
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&getModulePath), &self);
    GetModuleFileNameW(self, path, MAX_PATH);
    return path;
}

int normalizeFps(int v) {
    if (v == 0 || v == 30 || v == 60 || v == 120 || v == 144) {
        return v;
    }
    if (v < 0) {
        return 0;
    }
    if (v < 45) {
        return 30;
    }
    if (v < 90) {
        return 60;
    }
    if (v < 132) {
        return 120;
    }
    if (v < 200) {
        return 144;
    }
    return 0;
}

VisualQuality normalizeQuality(const wchar_t* s) {
    if (!s || !s[0]) {
        return VisualQuality::Cinema;
    }
    if (s[0] >= L'0' && s[0] <= L'2' && s[1] == 0) {
        return static_cast<VisualQuality>(s[0] - L'0');
    }
    if (_wcsicmp(s, L"ColorOnly") == 0 || _wcsicmp(s, L"color") == 0) {
        return VisualQuality::ColorOnly;
    }
    if (_wcsicmp(s, L"Balanced") == 0) {
        return VisualQuality::Balanced;
    }
    return VisualQuality::Cinema;
}

RemasterPreset normalizePreset(const wchar_t* s) {
    if (!s || !s[0] || _wcsicmp(s, L"Custom") == 0 || _wcsicmp(s, L"0") == 0) {
        return RemasterPreset::Custom;
    }
    if (_wcsicmp(s, L"Performance") == 0 || _wcsicmp(s, L"1") == 0) {
        return RemasterPreset::Performance;
    }
    if (_wcsicmp(s, L"Balanced") == 0 || _wcsicmp(s, L"2") == 0) {
        return RemasterPreset::Balanced;
    }
    return RemasterPreset::Ultra;
}

void applyPresetOverrides() {
    switch (g_config.preset) {
        case RemasterPreset::Performance:
            g_config.quality = VisualQuality::ColorOnly;
            g_config.visualStrength = 0.70f;
            g_config.anisotropic = true;
            g_config.anisotropy = 8;
            g_config.textureLodBias = -0.35f;
            g_config.casSharpen = 0.48f;
            g_config.lodBoost = true;
            g_config.lodHighDistance = 200.f;
            g_config.lodMediumDistance = 450.f;
            g_config.fovDegrees = 72;
            g_config.cityLife = true;
            g_config.trafficDensity = 1.2f;
            g_config.trafficMaxVehicles = 56;
            g_config.maxFakePeds = 80;
            g_config.teamMaxTokens = 6;
            break;
        case RemasterPreset::Balanced:
            g_config.quality = VisualQuality::Balanced;
            g_config.visualStrength = 0.80f;
            g_config.anisotropic = true;
            g_config.anisotropy = 16;
            g_config.textureLodBias = -0.45f;
            g_config.casSharpen = 0.52f;
            g_config.lodBoost = true;
            g_config.lodHighDistance = 350.f;
            g_config.lodMediumDistance = 750.f;
            g_config.fovDegrees = 74;
            g_config.cityLife = true;
            g_config.trafficDensity = 1.6f;
            g_config.trafficMaxVehicles = 72;
            g_config.maxFakePeds = 140;
            g_config.teamMaxTokens = 8;
            break;
        case RemasterPreset::Ultra:
            g_config.quality = VisualQuality::Cinema;
            g_config.visualStrength = 0.85f;
            g_config.anisotropic = true;
            g_config.anisotropy = 16;
            g_config.textureLodBias = -0.5f;
            g_config.casSharpen = 0.55f;
            g_config.lodBoost = true;
            g_config.lodHighDistance = 400.f;
            g_config.lodMediumDistance = 900.f;
            g_config.fovDegrees = 75;
            g_config.cityLife = true;
            g_config.trafficDensity = 2.0f;
            g_config.trafficMaxVehicles = 96;
            g_config.maxFakePeds = 200;
            g_config.teamMaxTokens = 10;
            break;
        case RemasterPreset::Custom:
        default:
            break;
    }
}

}  // namespace

Config& config() { return g_config; }

std::wstring moduleDirectory() {
    auto path = getModulePath();
    const auto pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) {
        return L".";
    }
    return path.substr(0, pos);
}

void loadConfig() {
    std::lock_guard lock(g_mutex);
    const auto ini = moduleDirectory() + L"\\sm3spectacular.ini";

    wchar_t buf[256]{};
    GetPrivateProfileStringW(L"sm3spectacular", L"FpsLimit", L"60", buf, 256, ini.c_str());
    g_config.fpsLimit = normalizeFps(_wtoi(buf));

    g_config.showFpsCounter =
        GetPrivateProfileIntW(L"sm3spectacular", L"ShowFpsCounter", 0, ini.c_str()) != 0;
    g_config.useReShade =
        GetPrivateProfileIntW(L"sm3spectacular", L"UseReShade", 1, ini.c_str()) != 0;
    // Default Visuals=0 in hybrid mode (ReShade owns the look).
    g_config.visuals = GetPrivateProfileIntW(L"sm3spectacular", L"Visuals", 0, ini.c_str()) != 0;
    if (g_config.useReShade) {
        // Avoid double grade/INTZ fighting Remastered depth.
        g_config.visuals = false;
    }

    GetPrivateProfileStringW(L"sm3spectacular", L"VisualStrength", L"0.85", buf, 256, ini.c_str());
    g_config.visualStrength = static_cast<float>(_wtof(buf));
    if (g_config.visualStrength < 0.f) g_config.visualStrength = 0.f;
    if (g_config.visualStrength > 1.5f) g_config.visualStrength = 1.5f;

    GetPrivateProfileStringW(L"sm3spectacular", L"Quality", L"Cinema", buf, 256, ini.c_str());
    g_config.quality = normalizeQuality(buf);

    GetPrivateProfileStringW(L"sm3spectacular", L"Preset", L"Custom", buf, 256, ini.c_str());
    g_config.preset = normalizePreset(buf);

    GetPrivateProfileStringW(L"sm3spectacular", L"DepthNear", L"0.15", buf, 256, ini.c_str());
    g_config.depthNear = static_cast<float>(_wtof(buf));
    GetPrivateProfileStringW(L"sm3spectacular", L"DepthFar", L"4000", buf, 256, ini.c_str());
    g_config.depthFar = static_cast<float>(_wtof(buf));
    g_config.depthReverse =
        GetPrivateProfileIntW(L"sm3spectacular", L"DepthReverse", 0, ini.c_str()) != 0;
    g_config.depthFlip = GetPrivateProfileIntW(L"sm3spectacular", L"DepthFlip", 0, ini.c_str()) != 0;

    g_config.fxaa = GetPrivateProfileIntW(L"sm3spectacular", L"Fxaa", 0, ini.c_str()) != 0;
    g_config.bloom = GetPrivateProfileIntW(L"sm3spectacular", L"Bloom", 1, ini.c_str()) != 0;
    g_config.ambientLight =
        GetPrivateProfileIntW(L"sm3spectacular", L"AmbientLight", 1, ini.c_str()) != 0;
    g_config.eyeAdapt = GetPrivateProfileIntW(L"sm3spectacular", L"EyeAdapt", 1, ini.c_str()) != 0;
    g_config.deband = GetPrivateProfileIntW(L"sm3spectacular", L"Deband", 1, ini.c_str()) != 0;
    GetPrivateProfileStringW(L"sm3spectacular", L"Clarity", L"0.28", buf, 256, ini.c_str());
    g_config.clarity = static_cast<float>(_wtof(buf));
    if (g_config.clarity < 0.f) g_config.clarity = 0.f;
    if (g_config.clarity > 1.f) g_config.clarity = 1.f;
    GetPrivateProfileStringW(L"sm3spectacular", L"Unsharp", L"0.33", buf, 256, ini.c_str());
    g_config.unsharp = static_cast<float>(_wtof(buf));
    if (g_config.unsharp < 0.f) g_config.unsharp = 0.f;
    if (g_config.unsharp > 1.f) g_config.unsharp = 1.f;
    GetPrivateProfileStringW(L"sm3spectacular", L"AoAmount", L"1.0", buf, 256, ini.c_str());
    g_config.aoAmount = static_cast<float>(_wtof(buf));
    if (g_config.aoAmount < 0.f) g_config.aoAmount = 0.f;
    if (g_config.aoAmount > 2.f) g_config.aoAmount = 2.f;
    GetPrivateProfileStringW(L"sm3spectacular", L"GiAmount", L"0.40", buf, 256, ini.c_str());
    g_config.giAmount = static_cast<float>(_wtof(buf));
    if (g_config.giAmount < 0.f) g_config.giAmount = 0.f;
    if (g_config.giAmount > 2.f) g_config.giAmount = 2.f;
    GetPrivateProfileStringW(L"sm3spectacular", L"FakeHdrAmount", L"0.28", buf, 256, ini.c_str());
    g_config.fakeHdrAmount = static_cast<float>(_wtof(buf));
    if (g_config.fakeHdrAmount < 0.f) g_config.fakeHdrAmount = 0.f;
    if (g_config.fakeHdrAmount > 1.f) g_config.fakeHdrAmount = 1.f;
    g_config.anisotropic =
        GetPrivateProfileIntW(L"sm3spectacular", L"Anisotropic", 1, ini.c_str()) != 0;
    g_config.anisotropy = GetPrivateProfileIntW(L"sm3spectacular", L"Anisotropy", 16, ini.c_str());
    if (g_config.anisotropy < 1) g_config.anisotropy = 1;
    if (g_config.anisotropy > 16) g_config.anisotropy = 16;

    GetPrivateProfileStringW(L"sm3spectacular", L"TextureLodBias", L"-0.25", buf, 256, ini.c_str());
    g_config.textureLodBias = static_cast<float>(_wtof(buf));
    if (g_config.textureLodBias < -1.5f) g_config.textureLodBias = -1.5f;
    if (g_config.textureLodBias > 1.f) g_config.textureLodBias = 1.f;

    GetPrivateProfileStringW(L"sm3spectacular", L"CasSharpen", L"0.55", buf, 256, ini.c_str());
    g_config.casSharpen = static_cast<float>(_wtof(buf));
    if (g_config.casSharpen < 0.f) g_config.casSharpen = 0.f;
    if (g_config.casSharpen > 1.f) g_config.casSharpen = 1.f;

    g_config.lodBoost = GetPrivateProfileIntW(L"sm3spectacular", L"LodBoost", 1, ini.c_str()) != 0;
    GetPrivateProfileStringW(L"sm3spectacular", L"LodHighDistance", L"400", buf, 256, ini.c_str());
    g_config.lodHighDistance = static_cast<float>(_wtof(buf));
    GetPrivateProfileStringW(L"sm3spectacular", L"LodMediumDistance", L"900", buf, 256, ini.c_str());
    g_config.lodMediumDistance = static_cast<float>(_wtof(buf));
    GetPrivateProfileStringW(L"sm3spectacular", L"LodInteriorDistance", L"50", buf, 256, ini.c_str());
    g_config.lodInteriorDistance = static_cast<float>(_wtof(buf));
    GetPrivateProfileStringW(L"sm3spectacular", L"LodDistrictCull", L"0", buf, 256, ini.c_str());
    g_config.lodDistrictCull = static_cast<float>(_wtof(buf));
    g_config.cityDetail = GetPrivateProfileIntW(L"sm3spectacular", L"CityDetail", 2, ini.c_str());
    GetPrivateProfileStringW(L"sm3spectacular", L"LodCityFar", L"40000", buf, 256, ini.c_str());
    g_config.lodCityFar = static_cast<float>(_wtof(buf));
    GetPrivateProfileStringW(L"sm3spectacular", L"LodCityNear", L"10", buf, 256, ini.c_str());
    g_config.lodCityNear = static_cast<float>(_wtof(buf));
    g_config.lodCityBudget =
        GetPrivateProfileIntW(L"sm3spectacular", L"LodCityBudget", 400000, ini.c_str());
    g_config.lodCityBudgetC =
        GetPrivateProfileIntW(L"sm3spectacular", L"LodCityBudgetC", 80000, ini.c_str());

    g_config.fovDegrees = GetPrivateProfileIntW(L"sm3spectacular", L"Fov", 75, ini.c_str());
    if (g_config.fovDegrees < 50) g_config.fovDegrees = 50;
    if (g_config.fovDegrees > 100) g_config.fovDegrees = 100;
    g_config.aspectFovCorrect =
        GetPrivateProfileIntW(L"sm3spectacular", L"AspectFovCorrect", 1, ini.c_str()) != 0;
    g_config.desktopResolution =
        GetPrivateProfileIntW(L"sm3spectacular", L"DesktopResolution", 0, ini.c_str()) != 0;
    g_config.displayWidth = GetPrivateProfileIntW(L"sm3spectacular", L"DisplayWidth", 0, ini.c_str());
    g_config.displayHeight =
        GetPrivateProfileIntW(L"sm3spectacular", L"DisplayHeight", 0, ini.c_str());
    g_config.fpsToggleVk = GetPrivateProfileIntW(L"sm3spectacular", L"FpsToggleVk", 0x77, ini.c_str());
    if (g_config.fpsToggleVk <= 0 || g_config.fpsToggleVk > 0xFE) {
        g_config.fpsToggleVk = 0x77;
    }
    g_config.fogDepth = GetPrivateProfileIntW(L"sm3spectacular", L"FogDepth", 1, ini.c_str()) != 0;
    g_config.fogVolumetric =
        GetPrivateProfileIntW(L"sm3spectacular", L"FogVolumetric", 1, ini.c_str()) != 0;
    GetPrivateProfileStringW(L"sm3spectacular", L"ParticleScale", L"3600", buf, 256, ini.c_str());
    g_config.particleScale = static_cast<float>(_wtof(buf));
    if (g_config.particleScale < 500.f) g_config.particleScale = 500.f;
    if (g_config.particleScale > 8000.f) g_config.particleScale = 8000.f;

    g_config.cityLife = GetPrivateProfileIntW(L"sm3spectacular", L"CityLife", 1, ini.c_str()) != 0;
    GetPrivateProfileStringW(L"sm3spectacular", L"TrafficDensity", L"2.0", buf, 256, ini.c_str());
    g_config.trafficDensity = static_cast<float>(_wtof(buf));
    if (g_config.trafficDensity < 0.f) g_config.trafficDensity = 0.f;
    if (g_config.trafficDensity > 3.f) g_config.trafficDensity = 3.f;
    g_config.trafficMaxVehicles =
        GetPrivateProfileIntW(L"sm3spectacular", L"TrafficMaxVehicles", 96, ini.c_str());
    if (g_config.trafficMaxVehicles < 5) g_config.trafficMaxVehicles = 5;
    if (g_config.trafficMaxVehicles > 128) g_config.trafficMaxVehicles = 128;
    g_config.trafficMaxLods =
        GetPrivateProfileIntW(L"sm3spectacular", L"TrafficMaxLods", 1500, ini.c_str());
    if (g_config.trafficMaxLods < 100) g_config.trafficMaxLods = 100;
    if (g_config.trafficMaxLods > 2000) g_config.trafficMaxLods = 2000;

    GetPrivateProfileStringW(L"sm3spectacular", L"TrafficSpawnMedium", L"280", buf, 256, ini.c_str());
    g_config.trafficSpawnMedium = static_cast<float>(_wtof(buf));
    GetPrivateProfileStringW(L"sm3spectacular", L"TrafficSpawnFar", L"420", buf, 256, ini.c_str());
    g_config.trafficSpawnFar = static_cast<float>(_wtof(buf));
    GetPrivateProfileStringW(L"sm3spectacular", L"TrafficSpawnVeryFar", L"1000", buf, 256, ini.c_str());
    g_config.trafficSpawnVeryFar = static_cast<float>(_wtof(buf));
    GetPrivateProfileStringW(L"sm3spectacular", L"TrafficLevelMedium", L"140", buf, 256, ini.c_str());
    g_config.trafficLevelMedium = static_cast<float>(_wtof(buf));
    GetPrivateProfileStringW(L"sm3spectacular", L"TrafficLevelFar", L"480", buf, 256, ini.c_str());
    g_config.trafficLevelFar = static_cast<float>(_wtof(buf));
    GetPrivateProfileStringW(L"sm3spectacular", L"TrafficLevelVeryFar", L"1000", buf, 256, ini.c_str());
    g_config.trafficLevelVeryFar = static_cast<float>(_wtof(buf));

    GetPrivateProfileStringW(L"sm3spectacular", L"TrafficVehicleSpeed", L"13", buf, 256, ini.c_str());
    g_config.trafficVehicleSpeed = static_cast<float>(_wtof(buf));
    GetPrivateProfileStringW(L"sm3spectacular", L"TrafficCopSpeed", L"32", buf, 256, ini.c_str());
    g_config.trafficCopSpeed = static_cast<float>(_wtof(buf));
    GetPrivateProfileStringW(L"sm3spectacular", L"TrafficThugSpeed", L"26", buf, 256, ini.c_str());
    g_config.trafficThugSpeed = static_cast<float>(_wtof(buf));
    g_config.trafficPromoteHero =
        GetPrivateProfileIntW(L"sm3spectacular", L"TrafficPromoteNearHero", 1, ini.c_str()) != 0;

    GetPrivateProfileStringW(L"sm3spectacular", L"PedsMaxVisible", L"-1", buf, 256, ini.c_str());
    g_config.pedsMaxVisible = _wtoi(buf);
    GetPrivateProfileStringW(L"sm3spectacular", L"MaxFakePeds", L"200", buf, 256, ini.c_str());
    g_config.maxFakePeds = _wtoi(buf);
    g_config.teamMaxTokens = GetPrivateProfileIntW(L"sm3spectacular", L"TeamMaxTokens", 10, ini.c_str());
    if (g_config.teamMaxTokens < 1) g_config.teamMaxTokens = 1;
    if (g_config.teamMaxTokens > 16) g_config.teamMaxTokens = 16;

    GetPrivateProfileStringW(L"sm3spectacular", L"EntitiesFadeIn", L"280", buf, 256, ini.c_str());
    g_config.entitiesFadeIn = static_cast<float>(_wtof(buf));
    GetPrivateProfileStringW(L"sm3spectacular", L"EntitiesFadeOut", L"400", buf, 256, ini.c_str());
    g_config.entitiesFadeOut = static_cast<float>(_wtof(buf));
    GetPrivateProfileStringW(L"sm3spectacular", L"EntitiesNoFadeRadius", L"180", buf, 256, ini.c_str());
    g_config.entitiesNoFadeRadius = static_cast<float>(_wtof(buf));

    g_config.steamCompat = GetPrivateProfileIntW(L"sm3spectacular", L"SteamCompat", 1, ini.c_str());
    if (g_config.steamCompat < 0 || g_config.steamCompat > 2) {
        g_config.steamCompat = 1;
    }
    g_config.steamForceWindowed =
        GetPrivateProfileIntW(L"sm3spectacular", L"SteamForceWindowed", 1, ini.c_str()) != 0;

    GetPrivateProfileStringW(L"sm3spectacular", L"ChainDLL", L"d3d9_reshade.dll", buf, 256, ini.c_str());
    g_config.chainDll = buf;
    if (g_config.useReShade && g_config.chainDll.empty()) {
        g_config.chainDll = L"d3d9_reshade.dll";
    }

    // Preset overrides knobs last — but CityLife=0 in ini always wins (mission-safe opt-out).
    const bool cityLifeFromIni = g_config.cityLife;
    applyPresetOverrides();
    if (!cityLifeFromIni) {
        g_config.cityLife = false;
    }
    // ReShade path wins again after presets (presets must not re-enable native visuals).
    if (g_config.useReShade) {
        g_config.visuals = false;
    }
}

void saveFpsLimitToIni() {
    std::lock_guard lock(g_mutex);
    const auto ini = moduleDirectory() + L"\\sm3spectacular.ini";
    wchar_t buf[32]{};
    _itow_s(g_config.fpsLimit, buf, 10);
    WritePrivateProfileStringW(L"sm3spectacular", L"FpsLimit", buf, ini.c_str());
}

}  // namespace sm3spectacular
