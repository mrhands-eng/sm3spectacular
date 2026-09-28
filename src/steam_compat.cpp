#include "steam_compat.hpp"

#include "config.hpp"
#include "log.hpp"

#include <Windows.h>
#include <TlHelp32.h>

#include <atomic>
#include <fstream>
#include <string>
#include <iterator>

namespace sm3spectacular {
namespace {

std::atomic<bool> g_steam{false};
std::atomic<bool> g_preferWindowed{false};

constexpr const char* kTechFull =
    "MXAO@qUINT_mxao.fx,SSR@qUINT_ssr.fx,HDR@FakeHDR.fx,AmbientLight@AmbientLight.fx,"
    "DeHaze@Dehaze.fx,DPX@DPX.fx,prod80_02_Cinetools_LUT@PD80_02_Cinetools_LUT.fx,"
    "prod80_02_Bloom@PD80_02_Bloom.fx,Deband@Deband.fx,SMAA@SMAA.fx,"
    "ContrastAdaptiveSharpen@CAS.fx,SpectacularFinish@SpectacularFinish.fx";

// Steam-safe: no MXAO/SSR (overlay breaks depth). Keep AL/Dehaze (no depth).
// Mild FilmicPass restores some depth/contrast lost without AO/SSR.
constexpr const char* kTechSteam =
    "HDR@FakeHDR.fx,AmbientLight@AmbientLight.fx,DeHaze@Dehaze.fx,"
    "DPX@DPX.fx,FilmicPass@FilmicPass.fx,"
    "prod80_02_Cinetools_LUT@PD80_02_Cinetools_LUT.fx,"
    "prod80_02_Bloom@PD80_02_Bloom.fx,Deband@Deband.fx,SMAA@SMAA.fx,"
    "ContrastAdaptiveSharpen@CAS.fx,SpectacularFinish@SpectacularFinish.fx";

bool moduleLoaded(const wchar_t* name) {
    return GetModuleHandleW(name) != nullptr;
}

bool detectSteam() {
    if (moduleLoaded(L"GameOverlayRenderer.dll") || moduleLoaded(L"GameOverlayRenderer64.dll") ||
        moduleLoaded(L"steamclient.dll") || moduleLoaded(L"steamclient64.dll")) {
        return true;
    }

    const DWORD pid = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return false;
    }
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    DWORD parent = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ProcessID == pid) {
                parent = pe.th32ParentProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    bool steamParent = false;
    if (parent && Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ProcessID == parent) {
                if (_wcsicmp(pe.szExeFile, L"steam.exe") == 0 ||
                    _wcsicmp(pe.szExeFile, L"steamwebhelper.exe") == 0) {
                    steamParent = true;
                }
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return steamParent;
}

void replaceLinePrefix(std::string& text, const char* prefix, const std::string& replacementLine) {
    const size_t prefixLen = std::char_traits<char>::length(prefix);
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t lineEnd = text.find('\n', pos);
        const size_t end = (lineEnd == std::string::npos) ? text.size() : lineEnd;
        // Compare against line without trailing \r
        size_t contentEnd = end;
        if (contentEnd > pos && text[contentEnd - 1] == '\r') {
            --contentEnd;
        }
        if (contentEnd >= pos + prefixLen &&
            text.compare(pos, prefixLen, prefix) == 0) {
            const bool hadNl = (lineEnd != std::string::npos);
            text.replace(pos, end - pos, replacementLine);
            pos += replacementLine.size();
            if (hadNl) {
                // ensure newline remains after replace if we didn't include one
                if (pos >= text.size() || text[pos] != '\n') {
                    // replacementLine has no \n; original \n was consumed in replace
                    // because end pointed at \n and we replaced [pos, end) excluding \n... 
                    // Wait: we used end = lineEnd which is index of \n, so replace length
                    // is end-pos and does NOT include \n. Good — \n stays.
                }
            }
            return;
        }
        pos = (lineEnd == std::string::npos) ? text.size() : lineEnd + 1;
    }
    // Prefix missing — append
    if (!text.empty() && text.back() != '\n') {
        text.push_back('\n');
    }
    text += replacementLine;
    text.push_back('\n');
}

bool lineEqualsPrefix(const std::string& text, const char* prefix, const std::string& wantFullLine) {
    const size_t prefixLen = std::char_traits<char>::length(prefix);
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t lineEnd = text.find('\n', pos);
        const size_t end = (lineEnd == std::string::npos) ? text.size() : lineEnd;
        size_t contentEnd = end;
        if (contentEnd > pos && text[contentEnd - 1] == '\r') {
            --contentEnd;
        }
        if (contentEnd >= pos + prefixLen && text.compare(pos, prefixLen, prefix) == 0) {
            return text.compare(pos, contentEnd - pos, wantFullLine) == 0;
        }
        pos = (lineEnd == std::string::npos) ? text.size() : lineEnd + 1;
    }
    return false;
}

// Returns: 1 wrote, 0 already matched (no write), -1 error.
int writePresetTechniques(const char* techniques) {
    const auto path = moduleDirectory() + L"\\ReShadePreset.ini";
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) {
        logf("steam: cannot open ReShadePreset.ini");
        return -1;
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    const std::string techLine = std::string("Techniques=") + techniques;
    const std::string sortLine = std::string("TechniqueSorting=") + techniques;
    if (lineEqualsPrefix(text, "Techniques=", techLine) &&
        lineEqualsPrefix(text, "TechniqueSorting=", sortLine)) {
        return 0;
    }

    replaceLinePrefix(text, "Techniques=", techLine);
    replaceLinePrefix(text, "TechniqueSorting=", sortLine);

    std::ofstream out(path.c_str(), std::ios::trunc | std::ios::binary);
    if (!out) {
        logf("steam: cannot write ReShadePreset.ini");
        return -1;
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return out.good() ? 1 : -1;
}

}  // namespace

bool steamDetected() { return g_steam.load(); }

bool steamPreferWindowed() { return g_preferWindowed.load(); }

void applySteamCompat() {
    try {
        const int mode = config().steamCompat;
        const bool steam = detectSteam() || mode == 2;
        g_steam.store(steam && mode != 0);

        if (mode == 0) {
            // Leave ReShadePreset.ini alone — user/manual Techniques stay as-is.
            logf("steam: compat disabled (SteamCompat=0) — preset not rewritten");
            g_preferWindowed.store(false);
            return;
        }

        if (!steam) {
            const int wr = writePresetTechniques(kTechFull);
            if (wr < 0) {
                logf("steam: not detected — full stack write failed");
            } else if (wr == 0) {
                logf("steam: not detected — full visual stack already set");
            } else {
                logf("steam: not detected — restored full visual stack");
            }
            g_preferWindowed.store(false);
            return;
        }

        g_preferWindowed.store(config().steamForceWindowed);
        const int wr = writePresetTechniques(kTechSteam);
        if (wr < 0) {
            logf("steam: DETECTED but preset write failed — continuing");
        } else if (wr == 0) {
            logf("steam: DETECTED — Steam-safe visuals already set. WindowedPrefer=%d",
                 (int)g_preferWindowed.load());
        } else {
            logf("steam: DETECTED — Steam-safe visuals (no MXAO/SSR). WindowedPrefer=%d",
                 (int)g_preferWindowed.load());
        }
        logf("steam: full MXAO/SSR available when launching Game.exe outside Steam");
    } catch (...) {
        logf("steam: compat exception swallowed — continuing without preset tweak");
        g_preferWindowed.store(false);
    }
}

}  // namespace sm3spectacular
