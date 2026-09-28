#include "exeid.hpp"

#include "log.hpp"

#include <Windows.h>

#include <cstdint>

namespace sm3spectacular {
namespace {

// Fingerprint of the Game.exe this mod was reverse-engineered against.
constexpr ULONGLONG kExpectedSize = 10862592ull;
constexpr DWORD kExpectedTimeDateStamp = 0x46D2EBDD;

bool readPeTimeDateStamp(const wchar_t* path, DWORD* outTs) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    bool ok = false;
    IMAGE_DOS_HEADER dos{};
    DWORD read = 0;
    if (ReadFile(h, &dos, sizeof(dos), &read, nullptr) && read == sizeof(dos) && dos.e_magic == IMAGE_DOS_SIGNATURE) {
        if (SetFilePointer(h, dos.e_lfanew, nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER) {
            DWORD sig = 0;
            if (ReadFile(h, &sig, sizeof(sig), &read, nullptr) && read == sizeof(sig) &&
                sig == IMAGE_NT_SIGNATURE) {
                IMAGE_FILE_HEADER fh{};
                if (ReadFile(h, &fh, sizeof(fh), &read, nullptr) && read == sizeof(fh)) {
                    *outTs = fh.TimeDateStamp;
                    ok = true;
                }
            }
        }
    }
    CloseHandle(h);
    return ok;
}

}  // namespace

bool exeIdentityOk() {
    static int cached = -1;
    if (cached >= 0) {
        return cached == 1;
    }

    wchar_t path[MAX_PATH]{};
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        logf("exeid: GetModuleFileName failed — VA patches DISABLED");
        cached = 0;
        return false;
    }

    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &fad)) {
        logf("exeid: cannot stat exe — VA patches DISABLED");
        cached = 0;
        return false;
    }
    ULARGE_INTEGER sz{};
    sz.LowPart = fad.nFileSizeLow;
    sz.HighPart = fad.nFileSizeHigh;

    DWORD ts = 0;
    if (!readPeTimeDateStamp(path, &ts)) {
        logf("exeid: cannot read PE timestamp — VA patches DISABLED");
        cached = 0;
        return false;
    }

    const bool ok = (sz.QuadPart == kExpectedSize && ts == kExpectedTimeDateStamp);
    if (ok) {
        logf("exeid: Game.exe OK size=%llu ts=0x%08lX", (unsigned long long)sz.QuadPart,
             (unsigned long)ts);
        cached = 1;
    } else {
        logf("exeid: UNKNOWN Game.exe size=%llu (want %llu) ts=0x%08lX (want 0x%08lX) — "
             "FPS/menu/LOD/CityLife VA patches DISABLED (d3d9 visuals still run)",
             (unsigned long long)sz.QuadPart, (unsigned long long)kExpectedSize, (unsigned long)ts,
             (unsigned long)kExpectedTimeDateStamp);
        cached = 0;
    }
    return cached == 1;
}

}  // namespace sm3spectacular
