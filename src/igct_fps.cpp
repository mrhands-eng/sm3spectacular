#include "igct_fps.hpp"

#include "log.hpp"

#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace sm3spectacular {
namespace {

constexpr wchar_t kKeyAutoExp[] = L"BX_3DMENU_VIDEO_OPTIONS_AUTOEXP=";
constexpr wchar_t kKeyFps60[] = L"BX_3DMENU_VIDEO_OPTIONS_FPS_60=";

const wchar_t* kFpsLines[] = {
    L"BX_3DMENU_VIDEO_OPTIONS_FPS_30=30",
    L"BX_3DMENU_VIDEO_OPTIONS_FPS_60=60",
    L"BX_3DMENU_VIDEO_OPTIONS_FPS_120=120",
    L"BX_3DMENU_VIDEO_OPTIONS_FPS_144=144",
    L"BX_3DMENU_VIDEO_OPTIONS_FPS_UNLIMITED=Unlimited",
};

std::wstring gameDirectory() {
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) {
        return {};
    }
    std::wstring p(path);
    const auto slash = p.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return {};
    }
    return p.substr(0, slash);
}

bool readFileBytes(const std::wstring& path, std::vector<uint8_t>& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > 8 * 1024 * 1024) {
        CloseHandle(h);
        return false;
    }
    out.resize(static_cast<size_t>(sz.QuadPart));
    DWORD read = 0;
    const BOOL ok = ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &read, nullptr);
    CloseHandle(h);
    return ok && read == out.size();
}

bool writeFileBytes(const std::wstring& path, const std::vector<uint8_t>& data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const BOOL ok =
        WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
    CloseHandle(h);
    return ok && written == data.size();
}

std::wstring decodeUtf16Le(const std::vector<uint8_t>& bytes, bool& hadBom) {
    hadBom = bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE;
    const size_t off = hadBom ? 2 : 0;
    if ((bytes.size() - off) % 2 != 0) {
        return {};
    }
    const size_t n = (bytes.size() - off) / 2;
    std::wstring out(n, L'\0');
    std::memcpy(out.data(), bytes.data() + off, n * sizeof(wchar_t));
    return out;
}

std::vector<uint8_t> encodeUtf16Le(const std::wstring& text, bool withBom) {
    std::vector<uint8_t> out;
    out.reserve((withBom ? 2 : 0) + text.size() * 2);
    if (withBom) {
        out.push_back(0xFF);
        out.push_back(0xFE);
    }
    const auto* p = reinterpret_cast<const uint8_t*>(text.data());
    out.insert(out.end(), p, p + text.size() * sizeof(wchar_t));
    return out;
}

bool lineStartsWith(const std::wstring& line, const wchar_t* prefix) {
    const size_t n = std::wcslen(prefix);
    return line.size() >= n && line.compare(0, n, prefix) == 0;
}

// Detect newline used in file (\r\n vs \n).
std::wstring detectNl(const std::wstring& text) {
    return (text.find(L"\r\n") != std::wstring::npos) ? L"\r\n" : L"\n";
}

// Returns true if file content was modified.
bool patchIgctText(std::wstring& text, bool russianSku) {
    if (text.find(kKeyFps60) != std::wstring::npos) {
        return false;
    }

    const std::wstring nl = detectNl(text);
    const wchar_t* autoExpValue = russianSku ? L"Лимит FPS" : L"FPS Limit";
    const std::wstring autoExpLine = std::wstring(kKeyAutoExp) + autoExpValue;

    std::wstring block = autoExpLine + nl;
    for (const wchar_t* fps : kFpsLines) {
        block += fps;
        block += nl;
    }

    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find(L'\n', pos);
        if (end == std::wstring::npos) {
            end = text.size();
        }
        size_t lineEnd = end;
        // Exclude \r before \n from line content.
        size_t contentEnd = end;
        if (contentEnd > pos && text[contentEnd - 1] == L'\r') {
            --contentEnd;
        }
        const std::wstring line = text.substr(pos, contentEnd - pos);
        if (lineStartsWith(line, kKeyAutoExp)) {
            // Replace AUTOEXP line (including its trailing newline) with AUTOEXP + FPS block.
            const size_t replaceLen = (end < text.size()) ? (end + 1 - pos) : (text.size() - pos);
            text.replace(pos, replaceLen, block);
            return true;
        }
        pos = (end == text.size()) ? text.size() : end + 1;
        (void)lineEnd;
    }

    if (!text.empty() && text.back() != L'\n') {
        text += nl;
    }
    text += block;
    return true;
}

bool patchOneIgct(const std::wstring& path) {
    std::vector<uint8_t> raw;
    if (!readFileBytes(path, raw)) {
        logf("igct: cannot read %ls", path.c_str());
        return false;
    }
    bool bom = false;
    std::wstring text = decodeUtf16Le(raw, bom);
    if (text.empty()) {
        logf("igct: not UTF-16 LE — skip %ls", path.c_str());
        return false;
    }

    const bool russianSku = path.find(L"igct_ru") != std::wstring::npos;

    if (!patchIgctText(text, russianSku)) {
        logf("igct: FPS keys already present — %ls", path.c_str());
        return true;
    }

    const std::wstring bak = path + L".stock.bak";
    if (GetFileAttributesW(bak.c_str()) == INVALID_FILE_ATTRIBUTES) {
        CopyFileW(path.c_str(), bak.c_str(), TRUE);
    }

    const auto out = encodeUtf16Le(text, true);
    if (!writeFileBytes(path, out)) {
        logf("igct: write failed %ls", path.c_str());
        return false;
    }
    logf("igct: patched FPS labels into %ls", path.c_str());
    return true;
}

}  // namespace

void ensureIgctFpsLabels() {
    const std::wstring root = gameDirectory();
    if (root.empty()) {
        return;
    }
    const std::wstring pattern = root + L"\\pcinterface\\igct_*.bnx";
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        logf("igct: no pcinterface\\igct_*.bnx found — FPS menu labels may be missing");
        return;
    }
    int n = 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        const std::wstring path = root + L"\\pcinterface\\" + fd.cFileName;
        if (patchOneIgct(path)) {
            ++n;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    logf("igct: processed %d locale file(s)", n);
}

}  // namespace sm3spectacular
