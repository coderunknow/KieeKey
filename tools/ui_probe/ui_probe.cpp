//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// File: tools/ui_probe/ui_probe.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//---------------------------------------------------------------------------
// v1.3.0-beta8 (CA-03) — the Windows UI probe.
//
// WHY THIS EXISTS
//   beta7 shipped with layout defects no local gate could see: the Linux suite
//   cannot create a real HWND, and scripts/audit_layout.py models the font
//   instead of measuring it. This probe runs INSIDE the real Windows UI: it
//   opens the shipped settings dialog through the app's own creation path
//   (KieeKeyProbeOpenSettings, compiled in only with -DKIEEKEY_UI_PROBE), walks
//   every tab and measures every control with the real font through
//   GetTextExtentPoint32W / DrawTextW.
//
// WHAT IT PROVES (per tab)
//   1. every visible control is inside its tab page             (outside_page)
//   2. no two visible controls overlap                          (overlap)
//   3. every label fits its box with the real font              (clip)
//   4. the vertical scrollbar is enabled EXACTLY when the solved content is
//      taller than the viewport — the BS-01 contract           (scrollbar)
//   5. every interactive control's centre hit-tests back to it  (hittest)
//   6. every tab header fits its item rectangle                 (tab_header)
//
// ARTEFACTS (written to the output directory, CI uploads them)
//   ui_probe.json  — every control (class/id/rect/text/font height) plus all
//                    findings, so a failure is diagnosable without a screen.
//   tab<N>.png     — one screenshot per tab (dependency-free PNG writer).
//
// WHAT IT DOES NOT PROVE (labelled MODELLED, never claimed as VERIFIED)
//   The runner has one real DPI (usually 96). The 100/125/150 % arithmetic
//   model stays with scripts/audit_layout.py (CA-01a) and the manual M1
//   checklist; this probe reports the DPI it actually measured.
//
// USAGE
//   kieekey_ui_probe [outDir]     -> exit 0 clean, 1 findings, 3 cannot run
//---------------------------------------------------------------------------
#if !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0A00   // GetDpiForWindow / SetProcessDpiAwarenessContext
#endif
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "resource.h"   // IDC_TAB — the same id the app uses

// The probe-only surface of src/app/main.cpp (compiled into this target).
extern "C" void KieeKeyProbeInit(HINSTANCE hInst);
extern "C" HWND KieeKeyProbeOpenSettings(int tab);
extern "C" int  KieeKeyProbeSelectTab(HWND dlg, int tab);
// CA-03 diagnostics: the app's OWN required text height for a control (the same
// DrawTextW call the solver uses), so the probe can compare instead of assume.
extern "C" int  KieeKeyProbeMeasureStaticHeight(HWND dlg, int id);
// CA-03: which page the APP assigns a control id to (-1 == always-visible
// chrome). Used to prove that two pages can never be on screen at once — the
// failure mode behind "every label is overwritten".
extern "C" int  KieeKeyProbeTabOfControl(HWND dlg, int id);
// CA-03: drive the REAL DPI-change path (child rescale + re-solve) with a forced
// DPI so the probe can audit the 125/150 % layouts a 96-dpi runner cannot make.
extern "C" int  KieeKeyProbeSimulateDpi(HWND dlg, UINT dpi);

// v1.3.0-beta8fix1 — the STATE-SEQUENCE surface. Four green rounds measured a
// settled dialog; these are what let the harness drive the app's own operations
// in a seeded order and read the app's OWN numbers back after each one.
struct ProbeScrollStateT {          // mirrors KieeKeyProbeScrollStateT (main.cpp)
    int haveBaseline;               // 0 => the scroll machinery has no layout
    int solvedCount;
    int offset;
    int range;
    int enabled;
    int styleVScroll;
    int viewportX, viewportY, viewportW, viewportH, viewportBottom;
    int contentBottom[9];
    int stripShift[9];              // tab-strip shift baked into the baseline (96 dpi)
    int stripSeen[9];               // the deepest that shift has ever been (96 dpi)
    int stripRelayouts;             // times the strip needed the nudge to re-lay out
    int barPos, barPage, barMax;
    UINT dpi;
    // v1.3.0-beta8fix1 (bug BS-22c): who last wrote each half of the
    // latch/WS_VSCROLL pair (see settingsApplyScrollbarLatch in main.cpp).
    const char* latchWriter;
    const char* styleWriter;
    int latchWrites;
    int styleWrites;
    int latchDrifts;
    int stripRows;                  // the last plan's row count (1 = one row)
};
extern "C" void KieeKeyProbeScrollState(HWND dlg, ProbeScrollStateT* out);
// v1.3.0-beta8fix1 (bug BS-22c): the app answers with the size of ITS struct, and
// this probe refuses to run when the two disagree — a field added on one side only
// shifts every field after it, and a `const char*` read out of an `int` is an
// access violation in the probe, not a finding. (That is exactly what happened to
// the first build of this round: the probe died before it could write its report,
// and the CI step said only "the UI probe did not write ui_probe.json".)
extern "C" unsigned KieeKeyProbeScrollStateSize(void);
extern "C" int  KieeKeyProbeSetOffset(HWND dlg, int pos);
extern "C" int  KieeKeyProbeDisplayChange(HWND dlg);
extern "C" void KieeKeyProbeSetWindowDpiOverride(UINT dpi);
extern "C" int  KieeKeyProbeResize(HWND dlg, int clientW, int clientH);
extern "C" int  KieeKeyProbeFontScale(HWND dlg, int percent);
// v1.3.0-beta8fix1 (bug BS-22g): the solver's own rectangle for a control.
extern "C" int  KieeKeyProbeSolvedRect(HWND dlg, int id, int* out);
extern "C" void KieeKeyProbeTypeRow(HWND dlg, int id, const wchar_t* text);

namespace {

int g_checks = 0;
int g_findings = 0;

struct Finding {
    std::string kind;
    std::string detail;
};

// One control, as the USER sees it. `region`/`onScreen`/`ex..eh` exist because
// the window-region state — not the window rectangle — decides what is painted:
// this dialog's page children are direct children of the dialog and "scroll" by
// moving + region-clipping, so a wrong region measures perfectly with
// GetWindowRect and paints garbage. Every geometry check below uses the
// EFFECTIVE rectangle, never the window rectangle.
struct Ctl {
    HWND        hwnd = nullptr;
    int         id = 0;
    std::string klass;
    std::string text;
    int         x = 0, y = 0, w = 0, h = 0;   // window rect, dialog client coords
    int         tabpage = -1;                  // the app's page (-1 == chrome)
    bool        groupBox = false;
    bool        interactive = false;
    bool        shown = false;                 // IsWindowVisible
    int         fontHeight = 0;
    bool        clipSiblings = false;          // WS_CLIPSIBLINGS (BS-16b)
    bool        hasRegion = false;             // a window region is set ...
    bool        regionEmpty = false;           // ... and it is empty (hidden)
    RECT        region{};                      // bounding box, child-local px
    bool        onScreen = false;              // region ∩ page is non-empty
    int         ex = 0, ey = 0, ew = 0, eh = 0;   // effective (visible) rectangle
};

//--------------------------------------------------------------- conversions
std::wstring toWide(const std::string& s) {
    if (s.empty()) { return {}; }
    const int need = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                           static_cast<int>(s.size()), nullptr, 0);
    if (need <= 0) { return {}; }
    std::wstring out(static_cast<std::size_t>(need), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                          out.data(), need);
    return out;
}

std::string toUtf8(const std::wstring& w) {
    if (w.empty()) { return {}; }
    const int need = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(),
                                           static_cast<int>(w.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (need <= 0) { return {}; }
    std::string out(static_cast<std::size_t>(need), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                          out.data(), need, nullptr, nullptr);
    return out;
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8]{};
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(ch);
                }
        }
    }
    return out;
}

//------------------------------------------------------------ measurements
std::string windowText(HWND h) {
    const int len = ::GetWindowTextLengthW(h);
    if (len <= 0) { return {}; }
    std::wstring buf(static_cast<std::size_t>(len) + 1, L'\0');
    const int got = ::GetWindowTextW(h, buf.data(), len + 1);
    buf.resize(got > 0 ? static_cast<std::size_t>(got) : 0);
    return toUtf8(buf);
}

std::string className(HWND h) {
    wchar_t buf[128]{};
    ::GetClassNameW(h, buf, static_cast<int>(sizeof(buf) / sizeof(buf[0])));
    return toUtf8(buf);
}

// v1.3.0-beta8fix1 (bug BS-22g): "solved XxY" — what the app's own solver put in
// the baseline for this control, empty when it has no baseline entry. Every
// rectangle finding carries it so the next reader does not have to guess whether
// the box came from the solver or from an older layout.
std::string solvedOf(HWND dlg, int id) {
    int r[4] = {0, 0, 0, 0};
    if (KieeKeyProbeSolvedRect(dlg, id, r) == 0) { return " solved -"; }
    // rectStr() is defined further down; same "x,y wxh" shape, spelled here so
    // this helper can sit next to the other id/geometry helpers.
    return " solved " + std::to_string(r[0]) + "," + std::to_string(r[1]) + " " +
           std::to_string(r[2]) + "x" + std::to_string(r[3]);
}

// The real measured width of `text` in `h`'s own font.
int textWidth(HWND h, const std::wstring& text) {
    if (h == nullptr || text.empty()) { return 0; }
    HDC dc = ::GetDC(h);
    if (dc == nullptr) { return 0; }
    HFONT font = reinterpret_cast<HFONT>(::SendMessageW(h, WM_GETFONT, 0, 0));
    HGDIOBJ old = font != nullptr ? ::SelectObject(dc, font) : nullptr;
    SIZE sz{};
    const BOOL ok = ::GetTextExtentPoint32W(dc, text.c_str(),
                                            static_cast<int>(text.size()), &sz);
    if (old != nullptr) { ::SelectObject(dc, old); }
    ::ReleaseDC(h, dc);
    return ok != FALSE ? static_cast<int>(sz.cx) : 0;
}

// Height the text needs inside `w` px — measured by DrawTextW, the same call
// the app's own solver uses for wrapped STATICs.
int wrappedTextHeight(HWND h, const std::wstring& text, int w) {
    if (h == nullptr || text.empty() || w <= 0) { return 0; }
    HDC dc = ::GetDC(h);
    if (dc == nullptr) { return 0; }
    HFONT font = reinterpret_cast<HFONT>(::SendMessageW(h, WM_GETFONT, 0, 0));
    HGDIOBJ old = font != nullptr ? ::SelectObject(dc, font) : nullptr;
    RECT r{0, 0, w, 0};
    // No DT_NOPREFIX: a STATIC interprets '&' as a prefix marker (and so does the
    // app's solver), so measuring with DT_NOPREFIX measures a string no user ever
    // sees — the second CI run turned that into two phantom "clip" findings on the
    // only two labels whose text contains '&'.
    ::DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &r,
                DT_CALCRECT | DT_WORDBREAK);
    if (old != nullptr) { ::SelectObject(dc, old); }
    ::ReleaseDC(h, dc);
    return static_cast<int>(r.bottom - r.top);
}

//=========================================================== PNG writer ====
// Dependency-free (no GDI+, no zlib): stored deflate blocks + CRC32. The shots
// are a few hundred kB each and only ever read by a human or by CI artefacts.
std::uint32_t crc32Of(const std::uint8_t* data, std::size_t len) {
    static std::uint32_t table[256];
    static bool built = false;
    if (!built) {
        for (std::uint32_t i = 0; i < 256U; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1U) != 0U ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
        built = true;
    }
    std::uint32_t c = 0xFFFFFFFFU;
    for (std::size_t i = 0; i < len; ++i) {
        c = table[(c ^ data[i]) & 0xFFU] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFU;
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(v & 0xFFU));
}

void putChunk(std::vector<std::uint8_t>& out, const char* tag,
              const std::vector<std::uint8_t>& body) {
    put32(out, static_cast<std::uint32_t>(body.size()));
    const std::size_t start = out.size();
    out.insert(out.end(), tag, tag + 4);
    out.insert(out.end(), body.begin(), body.end());
    put32(out, crc32Of(out.data() + start, out.size() - start));
}

// v1.3.0-beta8fix1 (bug BS-19): THE REPORT VALIDATES ITSELF.
//
// A hand-built JSON is one typo away from making the whole report unusable: a
// dropped '+' between two adjacent string literals is legal C++ (the literals
// concatenate) and produces a file no parser accepts. That is not hypothetical —
// growing this report with the BS-19 fields did exactly that, the workflow's
// ConvertFrom-Json threw, and the run died reporting "exit code 1" with NO
// annotation at all: a whole CI run lost to a missing '+'. The braces are
// therefore checked (outside string literals, escapes honoured) before anything
// is written; an unbalanced report is a LOUD failure with no file written, so
// the workflow's "did not write ui_probe.json" branch names it.
bool jsonBalanced(const std::string& s) {
    int depth = 0;
    bool inStr = false;
    bool esc = false;
    for (const char ch : s) {
        if (inStr) {
            if (esc) { esc = false; continue; }
            if (ch == '\\') { esc = true; continue; }
            if (ch == '"') { inStr = false; }
            continue;
        }
        switch (ch) {
            case '"': inStr = true; break;
            case '{': case '[': ++depth; break;
            case '}': case ']': --depth; if (depth < 0) { return false; } break;
            default: break;
        }
    }
    return depth == 0 && !inStr;
}

bool writeAll(const std::wstring& path, const void* data, std::size_t len) {
    HANDLE f = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) { return false; }
    DWORD written = 0;
    const bool ok = ::WriteFile(f, data, static_cast<DWORD>(len), &written, nullptr) != FALSE &&
                    static_cast<std::size_t>(written) == len;
    ::CloseHandle(f);
    return ok;
}

bool writePng(const std::wstring& path, int w, int h, const std::uint32_t* bgra) {
    if (w <= 0 || h <= 0 || bgra == nullptr) { return false; }
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(h) * (static_cast<std::size_t>(w) * 3U + 1U));
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);   // filter type: none
        for (int x = 0; x < w; ++x) {
            const std::uint32_t px =
                bgra[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                     static_cast<std::size_t>(x)];
            raw.push_back(static_cast<std::uint8_t>((px >> 16) & 0xFFU));
            raw.push_back(static_cast<std::uint8_t>((px >> 8) & 0xFFU));
            raw.push_back(static_cast<std::uint8_t>(px & 0xFFU));
        }
    }
    std::vector<std::uint8_t> z;
    z.push_back(0x78);
    z.push_back(0x01);
    std::size_t pos = 0;
    while (pos < raw.size()) {
        const std::size_t n = std::min<std::size_t>(65535U, raw.size() - pos);
        const bool last = pos + n >= raw.size();
        z.push_back(last ? 1U : 0U);
        z.push_back(static_cast<std::uint8_t>(n & 0xFFU));
        z.push_back(static_cast<std::uint8_t>((n >> 8) & 0xFFU));
        const std::uint16_t inv = static_cast<std::uint16_t>(~n);
        z.push_back(static_cast<std::uint8_t>(inv & 0xFFU));
        z.push_back(static_cast<std::uint8_t>((inv >> 8) & 0xFFU));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos),
                 raw.begin() + static_cast<std::ptrdiff_t>(pos + n));
        pos += n;
    }
    std::uint32_t a = 1U;
    std::uint32_t b = 0U;
    for (std::uint8_t byte : raw) {
        a = (a + byte) % 65521U;
        b = (b + a) % 65521U;
    }
    put32(z, (b << 16) | a);

    std::vector<std::uint8_t> png{0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU};
    std::vector<std::uint8_t> ihdr;
    put32(ihdr, static_cast<std::uint32_t>(w));
    put32(ihdr, static_cast<std::uint32_t>(h));
    ihdr.push_back(8);   // bit depth
    ihdr.push_back(2);   // truecolour
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    putChunk(png, "IHDR", ihdr);
    putChunk(png, "IDAT", z);
    putChunk(png, "IEND", std::vector<std::uint8_t>());
    return writeAll(path, png.data(), png.size());
}

// Renders the dialog (and children) into a 32-bpp DIB and writes it as PNG.
bool captureWindow(HWND hwnd, const std::wstring& path, int* outW, int* outH) {
    RECT rc{};
    if (::GetWindowRect(hwnd, &rc) == FALSE) { return false; }
    const int w = static_cast<int>(rc.right - rc.left);
    const int h = static_cast<int>(rc.bottom - rc.top);
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) { return false; }

    HDC screen = ::GetDC(nullptr);
    if (screen == nullptr) { return false; }
    HDC mem = ::CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;             // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = ::CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ::ReleaseDC(nullptr, screen);
    if (bmp == nullptr || bits == nullptr) {
        if (bmp != nullptr) { ::DeleteObject(bmp); }
        if (mem != nullptr) { ::DeleteDC(mem); }
        return false;
    }
    HGDIOBJ old = ::SelectObject(mem, bmp);
    // BS-16e: prefer the REAL screen. WM_PRINTCLIENT (the fallback) asks the app
    // to draw the window and always answers with a correct frame — it is a good
    // picture of the layout and a useless one of the desktop, which is where the
    // stale pixels live. `rc` is the whole window, so the capture includes the
    // frame exactly as the user sees it.
    bool shot = false;
    {
        HDC screen2 = ::GetDC(nullptr);
        if (screen2 != nullptr) {
            shot = ::BitBlt(mem, 0, 0, w, h, screen2, rc.left, rc.top, SRCCOPY) != FALSE;
            ::ReleaseDC(nullptr, screen2);
        }
    }
    if (!shot) {
        ::SendMessageW(hwnd, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(mem),
                       static_cast<LPARAM>(PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND));
    }
    ::GdiFlush();
    const bool ok = writePng(path, w, h, static_cast<const std::uint32_t*>(bits));
    if (old != nullptr) { ::SelectObject(mem, old); }
    ::DeleteObject(bmp);
    ::DeleteDC(mem);
    if (outW != nullptr) { *outW = w; }
    if (outH != nullptr) { *outH = h; }
    return ok;
}

// Translates a control's screen rectangle into dialog client coordinates.
// The audit (scripts/audit_layout.py TOUCH_TOLERANCE_PX) accepts a <= 4 px
// touch between neighbours: label boxes carry leading/descender space that no
// glyph reaches. The probe mirrors that rule so the two layers agree — the
// second CI run flagged 2 px label/control touches that the audit models as
// fine, which is noise, not a defect.
constexpr int kTouchTolerancePx = 4;

RECT clientRectOf(HWND parent, HWND child) {
    RECT r{};
    ::GetWindowRect(child, &r);
    POINT tl{r.left, r.top};
    POINT br{r.right, r.bottom};
    ::ScreenToClient(parent, &tl);
    ::ScreenToClient(parent, &br);
    return RECT{tl.x, tl.y, br.x, br.y};
}

bool rectEmpty(const RECT& r) { return r.right <= r.left || r.bottom <= r.top; }

RECT intersectRect(const RECT& a, const RECT& b) {
    RECT o{std::max(a.left, b.left), std::max(a.top, b.top),
           std::min(a.right, b.right), std::min(a.bottom, b.bottom)};
    return o;
}

std::string rectStr(int x, int y, int w, int h) {
    return std::to_string(x) + "," + std::to_string(y) + " " +
           std::to_string(w) + "x" + std::to_string(h);
}

std::string rectStr(const RECT& r) {
    return rectStr(r.left, r.top, r.right - r.left, r.bottom - r.top);
}

extern "C" void KieeKeyProbeFreezeUi(HWND dlg);   // main.cpp, probe build only
extern "C" void KieeKeyProbeReflowNow(HWND dlg);   // the app's growth reflow (BS-17)

// Reads the live geometry of every child of the dialog. `all` is the stable
// child list (refreshed geometry re-reads it after a scroll).
void readCtls(HWND dlg, const std::vector<HWND>& all, int currentTab,
              const RECT& page, std::vector<Ctl>& ctls) {
    ctls.clear();
    ctls.reserve(all.size());
    for (HWND child : all) {
        Ctl c;
        c.hwnd = child;
        c.id = ::GetDlgCtrlID(child);
        c.klass = className(child);
        c.text = windowText(child);
        const RECT r = clientRectOf(dlg, child);
        c.x = r.left;
        c.y = r.top;
        c.w = static_cast<int>(r.right - r.left);
        c.h = static_cast<int>(r.bottom - r.top);
        c.tabpage = KieeKeyProbeTabOfControl(dlg, c.id);
        c.shown = ::IsWindowVisible(child) != FALSE;
        const LONG_PTR style = ::GetWindowLongPtrW(child, GWL_STYLE);
        c.clipSiblings = (style & WS_CLIPSIBLINGS) != 0;
        // BS_GROUPBOX is 0x7 (a triplet of flag bits), so `style & BS_GROUPBOX`
        // is ALSO true for BS_DEFPUSHBUTTON (0x1) — which silently excluded the
        // OK button from the overlap and hit-test checks (a false negative that
        // hid the fourth stacked button of BS-09).
        c.groupBox = c.klass == "Button" && (style & BS_TYPEMASK) == BS_GROUPBOX;
        c.interactive = c.klass == "Button" || c.klass == "Edit" ||
                        c.klass == "ComboBox" || c.klass == "ListBox";
        // The window region is what the user actually sees. GetWindowRgn fails
        // (ERROR) when no region is set; NULLREGION means an EMPTY region — the
        // app's "move it out of sight without touching SW_SHOW/SW_HIDE".
        {
            HRGN rgn = ::CreateRectRgn(0, 0, 0, 0);
            if (rgn != nullptr) {
                const int type = ::GetWindowRgn(child, rgn);
                RECT box{};
                if (type != ERROR) {
                    c.hasRegion = true;
                    if (::GetRgnBox(rgn, &box) == NULLREGION) {
                        c.regionEmpty = true;
                        box = RECT{0, 0, 0, 0};
                    }
                    c.region = box;
                }
                ::DeleteObject(rgn);
            }
        }
        int rx = c.x;
        int ry = c.y;
        int rw = c.w;
        int rh = c.h;
        if (c.hasRegion) {
            rx = c.x + c.region.left;
            ry = c.y + c.region.top;
            rw = c.region.right - c.region.left;
            rh = c.region.bottom - c.region.top;
        }
        const RECT eff = intersectRect(RECT{rx, ry, rx + rw, ry + rh}, page);
        c.onScreen = c.shown && !rectEmpty(eff);
        c.ex = eff.left;
        c.ey = eff.top;
        c.ew = eff.right - eff.left;
        c.eh = eff.bottom - eff.top;
        // Font metrics + text (only for what the user can see).
        HDC dc = ::GetDC(child);
        if (dc != nullptr) {
            TEXTMETRICW tm{};
            HFONT font = reinterpret_cast<HFONT>(::SendMessageW(child, WM_GETFONT, 0, 0));
            HGDIOBJ oldFont = font != nullptr ? ::SelectObject(dc, font) : nullptr;
            if (::GetTextMetricsW(dc, &tm) != FALSE) {
                c.fontHeight = static_cast<int>(tm.tmHeight);
            }
            if (oldFont != nullptr) { ::SelectObject(dc, oldFont); }
            ::ReleaseDC(child, dc);
        }
        (void)currentTab;
        ctls.push_back(std::move(c));
    }
}

struct Audit {
    HWND   dlg = nullptr;
    HWND   tabsCtl = nullptr;
    RECT   client{};
    RECT   page{};
    int    tab = 0;
    int    scalePercent = 100;
    std::string prefix;                  // "" or "scroll@N: "
    std::vector<Finding>* findings = nullptr;
};

// ---- a) one page at a time -------------------------------------------------
void checkSinglePage(const Audit& a, const std::vector<Ctl>& ctls) {
    for (const Ctl& c : ctls) {
        ++g_checks;
        if (c.tabpage >= 0 && c.tabpage != a.tab && c.shown) {
            a.findings->push_back({"wrong_page",
                "id " + std::to_string(c.id) + " (" + c.klass + ") at " +
                rectStr(c.x, c.y, c.w, c.h) + " belongs to tab " +
                std::to_string(c.tabpage) + " but is VISIBLE while tab " +
                std::to_string(a.tab) + " is selected"});
        }
        if (c.tabpage == a.tab && !c.shown) {
            a.findings->push_back({"page_hidden",
                "id " + std::to_string(c.id) + " (" + c.klass + ") at " +
                rectStr(c.x, c.y, c.w, c.h) + " belongs to the selected tab " +
                std::to_string(a.tab) + " but is not visible"});
        }
    }
}

// ---- b) regions must agree with the page viewport --------------------------
// A control that the app wants to show must be visible EXACTLY where its
// rectangle is; a control it wants to hide or clip must be regioned to the
// expected clip. Any other region is a rendering bug that no rect-based check
// can see.
void checkRegions(const Audit& a, const std::vector<Ctl>& ctls) {
    if (!a.prefix.empty()) { return; }   // scroll steps move the page on purpose
    for (const Ctl& c : ctls) {
        if (c.tabpage < 0 || c.tabpage != a.tab) { continue; }
        if (!c.shown) { continue; }
        ++g_checks;
        const RECT want = intersectRect(RECT{c.x, c.y, c.x + c.w, c.y + c.h}, a.page);
        if (!c.hasRegion) {
            if (!rectEmpty(want)) { continue; }      // shown in full: correct
            a.findings->push_back({"region",
                "id " + std::to_string(c.id) + " (" + c.klass + ") is beyond the page (" +
                rectStr(c.x, c.y, c.w, c.h) + " vs page " + rectStr(a.page) +
                ") but has NO region, so it paints outside the viewport"});
            continue;
        }
        const RECT got = c.regionEmpty
            ? RECT{0, 0, 0, 0}
            : RECT{c.x + c.region.left, c.y + c.region.top,
                   c.x + c.region.right, c.y + c.region.bottom};
        if (rectEmpty(want) == c.regionEmpty) { continue; }
        if (std::abs(got.left - want.left) <= 1 && std::abs(got.top - want.top) <= 1 &&
            std::abs(got.right - want.right) <= 1 && std::abs(got.bottom - want.bottom) <= 1) {
            continue;
        }
        a.findings->push_back({"region",
            "id " + std::to_string(c.id) + " (" + c.klass + ") at " +
            rectStr(c.x, c.y, c.w, c.h) + " shows " +
            (c.regionEmpty ? std::string("nothing") : rectStr(got)) +
            " but the page " + rectStr(a.page) + " allows " +
            (rectEmpty(want) ? std::string("nothing") : rectStr(want))});
    }
}

// ---- c) no two visible controls overlap ------------------------------------
void checkOverlaps(const Audit& a, const std::vector<Ctl>& ctls) {
    const auto isChrome = [](const Ctl& c) { return c.tabpage < 0; };
    for (std::size_t i = 0; i < ctls.size(); ++i) {
        const Ctl& ci = ctls[i];
        if (!ci.onScreen || ci.groupBox) { continue; }
        for (std::size_t j = i + 1; j < ctls.size(); ++j) {
            const Ctl& cj = ctls[j];
            if (!cj.onScreen || cj.groupBox) { continue; }
            if (isChrome(ci) != isChrome(cj)) { continue; }
            ++g_checks;
            const int ix = std::min(ci.ex + ci.ew, cj.ex + cj.ew) - std::max(ci.ex, cj.ex);
            const int iy = std::min(ci.ey + ci.eh, cj.ey + cj.eh) - std::max(ci.ey, cj.ey);
            if (ix > kTouchTolerancePx && iy > kTouchTolerancePx) {
                a.findings->push_back({"overlap",
                    a.prefix + "id " + std::to_string(ci.id) + " (" + ci.klass + ") " +
                    rectStr(ci.ex, ci.ey, ci.ew, ci.eh) + " and id " +
                    std::to_string(cj.id) + " (" + cj.klass + ") " +
                    rectStr(cj.ex, cj.ey, cj.ew, cj.eh) + " overlap by " +
                    std::to_string(ix) + "x" + std::to_string(iy) + " px" +
                    solvedOf(a.dlg, ci.id) + solvedOf(a.dlg, cj.id) + " live " +
                    std::to_string(ci.h) + "/" + std::to_string(cj.h) + "px"});
            }
        }
        // A group box may contain its children, but it must never cover a
        // control it does not contain: that is the "text under a grey band"
        // the user sees and every rect-only audit misses.
        if (ci.onScreen && ci.groupBox) {
            for (const Ctl& cj : ctls) {
                if (&cj == &ci || !cj.onScreen || cj.groupBox) { continue; }
                if (cj.tabpage != ci.tabpage) { continue; }
                ++g_checks;
                const bool inside = cj.x >= ci.x && cj.y >= ci.y &&
                                    cj.x + cj.w <= ci.x + ci.w &&
                                    cj.y + cj.h <= ci.y + ci.h;
                if (inside) { continue; }
                if (ci.x < cj.x + cj.w && cj.x < ci.x + ci.w &&
                    ci.y < cj.y + cj.h && cj.y < ci.y + ci.h) {
                    a.findings->push_back({"group_overlap",
                        a.prefix + "group box id " + std::to_string(ci.id) + " " +
                        rectStr(ci.x, ci.y, ci.w, ci.h) + " covers id " +
                        std::to_string(cj.id) + " (" + cj.klass + ") " +
                        rectStr(cj.x, cj.y, cj.w, cj.h)});
                }
            }
        }
    }
}

// ---- d) everything reachable ----------------------------------------------
// Counts how often the desktop was unusable, so the digest says so out loud
// instead of looking like "0 findings" when nothing could be measured.
int g_screenUnavailable = 0;
// v1.3.0-beta8fix1 (bug BS-22i): passes where the tab strip cannot gain a row
// because the WINDOW cannot be made tall enough at that scale (see the strip
// cycle) — recorded with its numbers, never folded into a pass.
int g_stripCycleUnavailable = 0;
std::string g_stripCycleNote;
// How many times the screen comparison actually ran (vs. was skipped): "0
// findings" must never be read as "verified" when nothing was measured.
int g_screenCaptures = 0;

void checkReach(const Audit& a, const std::vector<Ctl>& ctls, int travelPx) {
    const int reachableBottom = a.page.bottom + travelPx;
    int contentBottom = a.page.top;
    for (const Ctl& c : ctls) {
        if (c.tabpage != a.tab || !c.shown) { continue; }
        contentBottom = std::max(contentBottom, c.y + c.h);
        ++g_checks;
        const bool beyond = c.y + c.h > reachableBottom + 1 || c.x + c.w > a.client.right + 1 ||
                            c.x < a.client.left - 1 || c.y < a.page.top - 1;
        if (beyond) {
            // The four bounds, spelled out: a finding whose own numbers look
            // inside the page is worse than no finding (the first version cost a
            // whole iteration guessing which of the four clauses fired).
            a.findings->push_back({"outside_page",
                "id " + std::to_string(c.id) + " (" + c.klass + ") at " +
                rectStr(c.x, c.y, c.w, c.h) + " is outside the reachable page (page " +
                rectStr(a.page) + " + " + std::to_string(travelPx) + " px of scroll; "
                "bottom " + std::to_string(c.y + c.h) + " vs " +
                std::to_string(reachableBottom + 1) + ", right " +
                std::to_string(c.x + c.w) + " vs " + std::to_string(a.client.right + 1) +
                ", left " + std::to_string(c.x) + " vs " +
                std::to_string(a.client.left - 1) + ", top " + std::to_string(c.y) +
                " vs " + std::to_string(a.page.top - 1) + ")"});
        }
    }
    // The scrollbar must be usable exactly when the page overflows, and the
    // deepest content must be reachable at the end of its travel.
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    const bool have = ::GetScrollInfo(a.dlg, SB_VERT, &si) != FALSE;
    const bool enabled = have && si.nPage > 0 && si.nMax > static_cast<int>(si.nPage) - 1;
    const bool wanted = contentBottom > a.page.bottom + 1;
    ++g_checks;
    if (enabled != wanted) {
        a.findings->push_back({"scrollbar",
            std::string("scrollbar ") + (enabled ? "enabled" : "disabled") +
            " but the page content ends at y=" + std::to_string(contentBottom) +
            ", the viewport ends at y=" + std::to_string(a.page.bottom) +
            (wanted ? " (content unreachable)" : " (no overflow)")});
    }
}

// ---- e) the visible text fits its (visible) box ----------------------------
// ---- f) the window is the rectangle the solver asked for -------------------
// v1.3.0-beta8fix1 (bug BS-22k): the harness half of this check lives in I6;
// this is the pass-audit half, which also covers the states the harness does not
// visit (the pass's own client, the scroll offsets) and carries the state prefix.
void checkPlanHeld(const Audit& a, const std::vector<Ctl>& ctls) {
    for (const Ctl& c : ctls) {
        if (c.tabpage < 0 || c.tabpage != a.tab || !c.shown || c.id == 0) { continue; }
        int solved[4] = {0, 0, 0, 0};
        if (KieeKeyProbeSolvedRect(a.dlg, c.id, solved) == 0 || solved[2] <= 0) { continue; }
        ++g_checks;
        const int dw = c.w - solved[2];
        const int dh = c.h - solved[3];
        if (dw > 1 || dw < -1 || dh > 1 || dh < -1) {
            a.findings->push_back({"win32_rect",
                a.prefix + "id " + std::to_string(c.id) + " (" + c.klass + ") lives " +
                rectStr(c.x, c.y, c.w, c.h) + " but the solver's baseline is " +
                rectStr(solved[0], solved[1], solved[2], solved[3]) + " (" +
                std::to_string(dw) + " px wider, " + std::to_string(dh) +
                " px taller) — the plan and the window disagree, so every row below "
                "is placed against a size this window does not have"});
        }
    }
}

void checkTextFit(const Audit& a, const std::vector<Ctl>& ctls) {
    for (const Ctl& c : ctls) {
        if (c.text.empty() || c.groupBox || !c.onScreen) { continue; }
        // v1.3.0-beta8 (probe): only judge a control the PAGE SHOWS WHOLE. A row
        // that extends below the viewport is clipped to the fold on purpose (the
        // user scrolls to read it) and calling that "text does not fit" is the
        // scrolling design, not a defect — the last x64 run reported exactly one
        // such finding twice (id 561: box 196 px, effective 134 = page bottom
        // 622 minus its y 488, with the app's own measurement agreeing on 196).
        if (c.y < a.page.top - 1 || c.y + c.h > a.page.bottom + 1) { continue; }
        const std::wstring wtext = toWide(c.text);
        ++g_checks;
        int need = 0;
        if (c.eh <= c.fontHeight + 4) {
            need = textWidth(c.hwnd, wtext);
            if (need > c.ew + 2) {
                // v1.3.0-beta8fix1 (bug BS-22g): the finding names the font the
                // text was measured in and how long the text is, because "needs
                // 744px in a 629px box" is only actionable with them: the row was
                // sized from the AUTHORED label while the control carries the
                // live text, and which of the two the app measured decides
                // whether this is a layout defect or a live-row contract.
                a.findings->push_back({"clip",
                    a.prefix + "id " + std::to_string(c.id) + " (" + c.klass + ") needs " +
                    std::to_string(need) + "px (app solver says " +
                    std::to_string(KieeKeyProbeMeasureStaticHeight(a.dlg, c.id)) +
                    "), shows " + std::to_string(c.ew) + "px of " + std::to_string(c.w) +
                    " (box " + rectStr(c.x, c.y, c.w, c.h) + ", font " +
                    std::to_string(c.fontHeight) + "px, text " +
                    std::to_string(wtext.size()) + " chars" +
                    solvedOf(a.dlg, c.id) + "): " + c.text.substr(0, 60)});
            }
        } else {
            need = wrappedTextHeight(c.hwnd, wtext, c.ew);
            if (need > c.eh + 2) {
                a.findings->push_back({"clip",
                    a.prefix + "id " + std::to_string(c.id) + " (" + c.klass + ") wraps to " +
                    std::to_string(need) + "px (app solver says " +
                    std::to_string(KieeKeyProbeMeasureStaticHeight(a.dlg, c.id)) +
                    ") in " + rectStr(c.ex, c.ey, c.ew, c.eh) + " (box " +
                    rectStr(c.x, c.y, c.w, c.h) + "): " + c.text.substr(0, 60)});
            }
        }
    }
}

// ---- f) every visible interactive control owns its centre ------------------
void checkHitTests(const Audit& a, const std::vector<Ctl>& ctls) {
    for (const Ctl& c : ctls) {
        if (!c.onScreen || !c.interactive || c.groupBox || c.ew <= 0 || c.eh <= 0) { continue; }
        const POINT pt{c.ex + c.ew / 2, c.ey + c.eh / 2};
        if (pt.x >= a.client.right || pt.y >= a.client.bottom || pt.x < 0 || pt.y < 0) { continue; }
        ++g_checks;
        POINT screenPt{pt.x, pt.y};
        ::ClientToScreen(a.dlg, &screenPt);
        const HWND hit = ::WindowFromPoint(screenPt);
        if (hit == nullptr) { continue; }
        if (hit == c.hwnd || ::IsChild(c.hwnd, hit) != FALSE) { continue; }
        // v1.3.0-beta8 (probe): only a hit inside OUR OWN window says anything
        // about our layout. The 150 % pass reported three `hittest` findings
        // whose centre landed on a window of class "Ghost" — created by the
        // shell, not by the app: another desktop window covering the dialog is
        // not a statement about where our controls are. A hit on the dialog
        // itself (id 0) or on a sibling control still is, and is still reported.
        if (hit != a.dlg && ::IsChild(a.dlg, hit) == FALSE) { continue; }
        a.findings->push_back({"hittest",
            a.prefix + "id " + std::to_string(c.id) + " (" + c.klass + ") centre (" +
            std::to_string(pt.x) + "," + std::to_string(pt.y) + ") hits id " +
            std::to_string(::GetDlgCtrlID(hit)) + " (" + className(hit) + ")"});
    }
}

// ---- h) a sibling paints through another one (BS-16b) ----------------------
//
// Every page child is a DIRECT child of the dialog, and the tab control's
// rectangle is the WHOLE page area: it is the lower-z sibling of ~120 controls
// at once. A window that does not carry WS_CLIPSIBLINGS is not clipped against
// the windows above it in z-order, so when it repaints it paints straight
// through them: leftover tab labels on top of diagnostics rows, half-erased
// labels that never come back (a static control only repaints when IT is
// invalidated, so a pixel it lost is lost until something moves it), and the
// user's "chữ bị duplicated / chữ bị kéo lên trên". Not one rectangle is wrong
// while this happens, which is why the whole geometry model — and the app's own
// self-check — stays green.
//
// The rule enforced here is the one the app now keeps: the tab control is part
// of the comparison, and any visible control whose effective rectangle really
// overlaps another visible one must clip against its siblings.
void checkSiblingClobber(const Audit& a, const std::vector<Ctl>& ctls) {
    struct Box {
        HWND hwnd; int id; std::string klass;
        int x, y, w, h; bool clip;
    };
    std::vector<Box> vis;
    vis.reserve(ctls.size() + 1);
    for (const Ctl& c : ctls) {
        if (!c.onScreen || c.ew <= 0 || c.eh <= 0) { continue; }
        vis.push_back(Box{c.hwnd, c.id, c.klass, c.ex, c.ey, c.ew, c.eh, c.clipSiblings});
    }
    // The tab control is excluded from the child list (it is the page's
    // container) — include it here, it is the worst offender by area.
    if (a.tabsCtl != nullptr && ::IsWindowVisible(a.tabsCtl) != FALSE) {
        RECT r{};
        if (::GetWindowRect(a.tabsCtl, &r) != FALSE) {
            POINT tl{r.left, r.top};
            POINT br{r.right, r.bottom};
            ::ScreenToClient(a.dlg, &tl);
            ::ScreenToClient(a.dlg, &br);
            const long style = ::GetWindowLongW(a.tabsCtl, GWL_STYLE);
            vis.push_back(Box{a.tabsCtl, ::GetDlgCtrlID(a.tabsCtl), className(a.tabsCtl),
                              static_cast<int>(tl.x), static_cast<int>(tl.y),
                              static_cast<int>(br.x - tl.x), static_cast<int>(br.y - tl.y),
                              (style & WS_CLIPSIBLINGS) != 0});
        }
    }
    for (std::size_t i = 0; i < vis.size(); ++i) {
        for (std::size_t j = i + 1; j < vis.size(); ++j) {
            const Box& bi = vis[i];
            const Box& bj = vis[j];
            const int ix = std::min(bi.x + bi.w, bj.x + bj.w) - std::max(bi.x, bj.x);
            const int iy = std::min(bi.y + bi.h, bj.y + bj.h) - std::max(bi.y, bj.y);
            if (ix <= kTouchTolerancePx || iy <= kTouchTolerancePx) { continue; }
            ++g_checks;
            if (bi.clip && bj.clip) { continue; }
            const Box& bad = bi.clip ? bj : bi;
            a.findings->push_back({"clobber",
                a.prefix + "id " + std::to_string(bad.id) + " (" + bad.klass +
                ") " + rectStr(bad.x, bad.y, bad.w, bad.h) +
                " overlaps id " + std::to_string((bi.clip ? bi : bj).id) + " (" +
                (bi.clip ? bi : bj).klass + ") " +
                rectStr((bi.clip ? bi : bj).x, (bi.clip ? bi : bj).y,
                        (bi.clip ? bi : bj).w, (bi.clip ? bi : bj).h) +
                " by " + std::to_string(ix) + "x" + std::to_string(iy) +
                " px and does not clip against its siblings (WS_CLIPSIBLINGS)" +
                " — its repaint paints through the control above it"});
        }
    }
}

// ---- j) the desktop holds what the app painted (BS-16e) --------------------
//
// Everything the probe ever "saw" was drawn on demand: WM_PRINTCLIENT renders
// the frame the app WOULD produce, correctly, every time. The user's screen
// holds the frame the app DID leave behind, and that is a different object: a
// control that moved left its pixels, a hidden tab's controls are still drawn,
// the tab control painted through the page. No rectangle check can see any of
// it — which is why CI was green on a screen the user photographed as broken.
//
// There is exactly one way to test it: read the screen, ask the app for a full
// repaint of the whole subtree, read the screen again, and compare. A window
// that owns its pixels is bit-identical; every differing pixel is a pixel the
// app left behind. (A headless or fully occluded session returns a uniform
// bitmap — the check then reports itself as unavailable instead of passing.)
bool readScreenClient(HWND hwnd, std::vector<std::uint32_t>* px, int* w, int* h) {
    RECT client{};
    if (::GetClientRect(hwnd, &client) == FALSE) { return false; }
    const int cw = static_cast<int>(client.right - client.left);
    const int ch = static_cast<int>(client.bottom - client.top);
    if (cw <= 0 || ch <= 0 || cw > 4096 || ch > 4096) { return false; }
    POINT org{client.left, client.top};
    if (::ClientToScreen(hwnd, &org) == FALSE) { return false; }

    HDC screen = ::GetDC(nullptr);
    if (screen == nullptr) { return false; }
    HDC mem = ::CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = cw;
    bi.bmiHeader.biHeight = -ch;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = ::CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    bool ok = false;
    if (bmp != nullptr && bits != nullptr && mem != nullptr) {
        HGDIOBJ old = ::SelectObject(mem, bmp);
        if (::BitBlt(mem, 0, 0, cw, ch, screen, org.x, org.y, SRCCOPY) != FALSE) {
            ::GdiFlush();
            px->assign(static_cast<const std::uint32_t*>(bits),
                       static_cast<const std::uint32_t*>(bits) + static_cast<std::size_t>(cw) * ch);
            *w = cw;
            *h = ch;
            ok = true;
        }
        if (old != nullptr) { ::SelectObject(mem, old); }
    }
    if (bmp != nullptr) { ::DeleteObject(bmp); }
    if (mem != nullptr) { ::DeleteDC(mem); }
    ::ReleaseDC(nullptr, screen);
    return ok;
}

// v1.3.0-beta8fix1 (bug BS-22g): WHAT THE APP WOULD DRAW AT THESE POINTS.
// WM_PRINTCLIENT always answers with a correct frame — that is the whole reason
// I11 reads the real screen — so it is the control for a screen capture that
// reads as background everywhere: if the render paints where the screen does not,
// the frame on the desktop is not the frame the app draws (occlusion, a sibling
// that repainted over the page, a capture of another window), and if it paints
// nothing either, the app really has no content there.
int renderPaintCountAt(HWND hwnd, const std::vector<POINT>& pts, std::uint32_t bg) {
    if (hwnd == nullptr || pts.empty()) { return 0; }
    RECT client{};
    if (::GetClientRect(hwnd, &client) == FALSE) { return 0; }
    const int cw = static_cast<int>(client.right - client.left);
    const int ch = static_cast<int>(client.bottom - client.top);
    if (cw <= 0 || ch <= 0 || cw > 4096 || ch > 4096) { return 0; }
    HDC screen = ::GetDC(nullptr);
    if (screen == nullptr) { return 0; }
    HDC mem = ::CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = cw;
    bi.bmiHeader.biHeight = -ch;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = ::CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    int painted = 0;
    if (bmp != nullptr && bits != nullptr && mem != nullptr) {
        HGDIOBJ old = ::SelectObject(mem, bmp);
        ::SendMessageW(hwnd, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(mem),
                       static_cast<LPARAM>(PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND));
        ::GdiFlush();
        const auto* px = static_cast<const std::uint32_t*>(bits);
        for (const POINT& pt : pts) {
            if (pt.x < 0 || pt.y < 0 || pt.x >= cw || pt.y >= ch) { continue; }
            if ((px[static_cast<std::size_t>(pt.y) * cw + pt.x] & 0x00FFFFFFU) !=
                (bg & 0x00FFFFFFU)) {
                ++painted;
            }
        }
        if (old != nullptr) { ::SelectObject(mem, old); }
    }
    if (bmp != nullptr) { ::DeleteObject(bmp); }
    if (mem != nullptr) { ::DeleteDC(mem); }
    ::ReleaseDC(nullptr, screen);
    return painted;
}

// A uniform bitmap means the session has no visible desktop (or our window is
// completely covered): the pixel comparison would compare nothing to nothing.
bool screenIsUsable(const std::vector<std::uint32_t>& px) {
    if (px.empty()) { return false; }
    std::uint32_t first = px.front() & 0x00FFFFFFU;
    int different = 0;
    for (const std::uint32_t p : px) {
        if ((p & 0x00FFFFFFU) != first) { ++different; }
    }
    return different > static_cast<int>(px.size() / 100);   // >1 % of the area
}

void checkStalePixels(const Audit& a) {
    if (a.dlg == nullptr) { return; }
    ::Sleep(40);   // let the composer settle before the reference frame
    std::vector<std::uint32_t> before;
    std::vector<std::uint32_t> after;
    int w = 0;
    int h = 0;
    if (!readScreenClient(a.dlg, &before, &w, &h) || !screenIsUsable(before)) {
        ++g_screenUnavailable;
        return;
    }
    ++g_screenCaptures;
    ::RedrawWindow(a.dlg, nullptr, nullptr,
                   RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN |
                   RDW_UPDATENOW | RDW_FRAME);
    ::UpdateWindow(a.dlg);
    ::Sleep(60);
    if (!readScreenClient(a.dlg, &after, &w, &h) || after.size() != before.size()) { return; }
    ++g_checks;
    int diff = 0;
    int firstX = -1;
    int firstY = -1;
    for (std::size_t i = 0; i < before.size(); ++i) {
        if (before[i] == after[i]) { continue; }
        ++diff;
        if (firstX < 0) {
            firstX = static_cast<int>(i % static_cast<std::size_t>(w));
            firstY = static_cast<int>(i / static_cast<std::size_t>(w));
        }
    }
    if (diff == 0) { return; }
    a.findings->push_back({"stale_pixels",
        a.prefix + std::to_string(diff) + " of " + std::to_string(w * h) +
        " client pixels changed after a forced full repaint (first at " +
        std::to_string(firstX) + "," + std::to_string(firstY) +
        ") — what the desktop held was not what the app draws: pixels of moved," +
        " hidden or painted-through controls were left behind"});
}

// ---- j) a page with nothing on it (BS-17) ---------------------------------
//
// The single most visible defect a user can report: the page area is empty. It
// is also invisible to every other check here — "no overlap", "nothing outside
// the page" and "no stale pixels" are all satisfied by a page that draws
// nothing at all, and the geometry digest happily lists 120 controls that are
// each off screen for a reason of their own. One of them has to be visible: a
// tab with content and an empty viewport is a defect, not a layout.
void checkEmptyPage(const Audit& a, const std::vector<Ctl>& ctls, int travelPx) {
    int visible = 0;
    int pageControls = 0;
    int firstTop = -1;      // the top-most control of the tab, visible or not
    for (const Ctl& c : ctls) {
        if (c.tabpage != a.tab) { continue; }
        ++pageControls;
        firstTop = (firstTop < 0) ? c.y : std::min(firstTop, c.y);
        if (c.onScreen) { ++visible; }
    }
    if (pageControls == 0) { return; }     // a tab with no controls is not blank
    ++g_checks;
    if (visible == 0) {
        // The most specific message wins: content that starts BELOW the page is
        // the signature of a layout that drifted away (BS-17), content spread
        // around with nothing showing is a region/geometry defect.
        const std::string where =
            (firstTop > a.page.bottom)
                ? ("the whole tab starts below the page bottom (first control at y=" +
                   std::to_string(firstTop) + ", page " +
                   rectStr(a.page.left, a.page.top, a.page.right - a.page.left,
                           a.page.bottom - a.page.top) +
                   ") — the content has drifted out of the viewport")
                : std::string("the controls of this tab are all outside the page"
                              " rectangle for a reason of their own");
        a.findings->push_back({"empty_page",
            a.prefix + "none of the " + std::to_string(pageControls) +
            " controls of this tab is visible (scroll travel " +
            std::to_string(travelPx) + "px): " + where});
    }
}

// ---- k) a reflow while the page is scrolled (BS-17) ------------------------
//
// The app re-solves the layout whenever a live row needs more room, and it does
// so at whatever scroll offset the user is at. That re-solve must be a function
// of the LAYOUT, never of the render: the pre-fix solver read its input geometry
// from the live window rectangles, so the scroll offset was folded into the
// layout and every reflow dragged the page up by up to `offset` px (and shrank
// the reported content depth with it, until the range collapsed to nothing and
// the page was empty). CI never saw it: every other measurement here happens on
// a freshly solved dialog at offset 0.
//
// The check drives the app's own reflow entry point at half travel and asserts
// what the user's eyes assert: nothing moved up, no control disappeared, the
// content depth did not shrink, and the scroll position survived.
void checkReflowWhileScrolled(const Audit& a, HWND dlg, const std::vector<HWND>& all,
                              const RECT& page, std::vector<Ctl>& ctls, int travelPx) {
    if (a.dlg == nullptr || travelPx <= 0) { return; }
    const int want = travelPx / 2;
    int guard = 0;
    while (guard++ < 128) {
        SCROLLINFO si{};
        si.cbSize = sizeof(si);
        si.fMask = SIF_POS;
        if (::GetScrollInfo(dlg, SB_VERT, &si) == FALSE) { return; }
        if (static_cast<int>(si.nPos) >= want) { break; }
        ::SendMessageW(dlg, WM_VSCROLL, MAKEWPARAM(SB_LINEDOWN, 0), 0);
    }
    SCROLLINFO at{};
    at.cbSize = sizeof(at);
    at.fMask = SIF_POS;
    if (::GetScrollInfo(dlg, SB_VERT, &at) == FALSE) { return; }
    const int offsetBefore = static_cast<int>(at.nPos);
    if (offsetBefore <= 0) { return; }

    readCtls(dlg, all, a.tab, page, ctls);
    std::vector<Ctl> before = ctls;
    KieeKeyProbeReflowNow(dlg);          // the app's own growth reflow path
    ::Sleep(20);
    readCtls(dlg, all, a.tab, page, ctls);

    SCROLLINFO afterSc{};
    afterSc.cbSize = sizeof(afterSc);
    afterSc.fMask = SIF_POS;
    const int offsetAfter = (::GetScrollInfo(dlg, SB_VERT, &afterSc) != FALSE)
                                ? static_cast<int>(afterSc.nPos) : -1;
    ++g_checks;
    if (offsetAfter < 0 || offsetAfter < offsetBefore - 2) {
        a.findings->push_back({"reflow_moved",
            a.prefix + "the reflow dropped the scroll position from " +
            std::to_string(offsetBefore) + " to " + std::to_string(offsetAfter)});
    }

    int movedUp = 0;
    int firstUp = 0;
    int firstUpBy = 0;
    int depthBefore = page.top;
    int depthAfter = page.top;
    for (const Ctl& b : before) {
        if (b.tabpage != a.tab || !b.onScreen) { continue; }
        depthBefore = std::max(depthBefore, b.y + b.h);
        for (const Ctl& f : ctls) {
            if (f.id != b.id) { continue; }
            depthAfter = std::max(depthAfter, f.y + f.h);
            // Growing a row pushes what is below it DOWN; nothing may move up,
            // and it may not move sideways at all.
            if (f.y < b.y - 2 || f.x != b.x) {
                ++movedUp;
                if (firstUp == 0) { firstUp = b.id; firstUpBy = b.y - f.y; }
            }
            break;
        }
    }
    ++g_checks;
    if (movedUp > 0) {
        a.findings->push_back({"reflow_moved",
            a.prefix + std::to_string(movedUp) + " control(s) moved UP when the app "
            "re-solved the layout at scroll offset " + std::to_string(offsetBefore) +
            " (first: id " + std::to_string(firstUp) + " by " +
            std::to_string(firstUpBy) + "px) — the reflow folded the scroll offset "
            "into the layout: the page drifts away and the content depth shrinks "
            "with it until the page is empty and the range collapses"});
    }
    ++g_checks;
    if (depthAfter < depthBefore - 2) {
        a.findings->push_back({"reflow_moved",
            a.prefix + "the content depth shrank from " + std::to_string(depthBefore) +
            " to " + std::to_string(depthAfter) + "px across a reflow at offset " +
            std::to_string(offsetBefore) + " — the scroll range loses exactly what "
            "the page loses"});
    }
    // NOTE: a reflow may legitimately hide a row (a row that GREW pushes one
    // past the fold). The invariant is about the layout, not about the count:
    // nothing moves up and the content depth never shrinks.
    ::SendMessageW(dlg, WM_VSCROLL, MAKEWPARAM(SB_TOP, 0), 0);
    ::Sleep(10);
    readCtls(dlg, all, a.tab, page, ctls);
}

// ---- g) the tab headers are readable ---------------------------------------
void checkTabHeaders(const Audit& a, int tabCount, std::vector<Finding>* findings) {
    if (a.tabsCtl == nullptr) { return; }
    for (int i = 0; i < tabCount; ++i) {
        wchar_t label[128]{};
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = label;
        item.cchTextMax = static_cast<int>(sizeof(label) / sizeof(label[0]));
        RECT ir{};
        ++g_checks;
        if (::SendMessageW(a.tabsCtl, TCM_GETITEMW, static_cast<WPARAM>(i),
                           reinterpret_cast<LPARAM>(&item)) == FALSE ||
            ::SendMessageW(a.tabsCtl, TCM_GETITEMRECT, static_cast<WPARAM>(i),
                           reinterpret_cast<LPARAM>(&ir)) == FALSE) {
            continue;
        }
        const int need = textWidth(a.tabsCtl, label);
        if (need + 8 > static_cast<int>(ir.right - ir.left)) {
            findings->push_back({"tab_header",
                "tab " + std::to_string(i) + " needs " + std::to_string(need) +
                "px in " + std::to_string(static_cast<int>(ir.right - ir.left)) + "px"});
        }
    }
    (void)findings;
}

//===========================================================================
// v1.3.0-beta8fix1 (BS-18) — THE OPERATION-SEQUENCE HARNESS.
//
// WHY. Four consecutive CI rounds were green on a screen the user had
// photographed as corrupted: every check in this file measured a SETTLED dialog
// (solve, then look), and the defect lives in the ORDER of operations — one
// operation leaves the dialog coherent, the next leaves it with NO layout
// baseline and a scroll state from the previous geometry, and from then on the
// page is unreachable: nothing re-solves, every scroll step is a silent no-op
// (the thumb moves, the page does not), the scrollbar either lies or is gone,
// and the content keeps whatever rectangles the last operation gave it.
//
// WHAT. After EVERY operation the harness asserts the page invariants on the
// app's OWN numbers (KieeKeyProbeScrollState) plus the live rectangles and
// window regions:
//
//   I1  offset 0 => at least one page control of the current tab is visible
//   I2  no visible page control starts above the page top
//   I3  the app's stored content depth matches its own deepest control
//   I4  range == contentBottom - viewportBottom (recomputed, never latched)
//   I5  the bar latch, the WS_VSCROLL bit and Win32's own enable rule agree
//   I6  a control inside the viewport is never region-clipped to nothing
//   I7  at offset 0 a control fully inside the viewport carries no region
//   I8  a forced full repaint changes zero client pixels      (scenario, screen)
//   I9  a reflow never moves a control up/sideways, never shrinks the depth
//   I10 returning to offset 0 restores the settled visible set
//   I11 the page paints content, not background only          (scenario, screen)
//   I12 the app's own text measurement fits the control's box
//
// The operations are the app's own paths: real messages (WM_VSCROLL,
// WM_MOUSEWHEEL, WM_TIMER, TCN_SELCHANGE), the touchpad-default tail of the
// thumb drag (KieeKeyProbeSetOffset), the DPI path, the display-change response
// and the tick's growth request. Seeded and fixed-count: no wall clock, no
// runner influence, same trace for the same build.
//===========================================================================

int g_fuzzSteps = 0;
int g_fuzzChecks = 0;
int g_fuzzFailures = 0;
int g_scenarioRuns = 0;
int g_scenarioFailures = 0;
int g_traceLines = 0;
int g_invChecks[14] = {};
int g_invFailures[14] = {};
std::string g_trace;

void harnessTrace(const std::string& line) {
    if (g_traceLines >= 500) { return; }
    ++g_traceLines;
    g_trace += line;
    g_trace += "\n";
}

void harnessKind(std::vector<std::pair<std::string, int>>* byKind,
                 const std::string& kind) {
    for (auto& e : *byKind) {
        if (e.first == kind) { ++e.second; return; }
    }
    byKind->emplace_back(kind, 1);
}

// Deterministic xorshift32 with a fixed seed list: the harness must reproduce
// the same sequence on every machine and every run.
struct HarnessRng {
    unsigned s = 1;
    explicit HarnessRng(unsigned seed) : s(seed != 0U ? seed : 1U) {}
    unsigned next() {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return s;
    }
    int pick(int lo, int hi) {
        if (hi <= lo) { return lo; }
        return lo + static_cast<int>(next() % static_cast<unsigned>(hi - lo + 1));
    }
};

struct HarnessCtl {
    HWND hwnd = nullptr;
    int  id = 0;
    bool shown = false;
    bool hasRegion = false;
    bool regionEmpty = false;
    int  x = 0, y = 0, w = 0, h = 0;       // window rect, dialog client coords
    int  rl = 0, rt = 0, rr = 0, rb = 0;   // region box (child-local)
    int  ex = 0, ey = 0, ew = 0, eh = 0;   // region box ∩ page
    // v1.3.0-beta8fix1 (bug BS-22c): the class the SOLVER measures and grows
    // (a STATIC that is not a group box / icon / owner-draw) — see I12.
    bool growable = false;
};

struct HarnessState {
    RECT page{};
    RECT client{};
    ProbeScrollStateT app{};
    std::vector<HarnessCtl> ctls;
    int  tab = 0;
    int  deepestUnscrolledBottom = 0;
    int  visibleCount = 0;
    bool havePage = false;
};

bool livePageRect(HWND dlg, RECT* page, RECT* client) {
    HWND tabs = ::GetDlgItem(dlg, IDC_TAB);
    if (tabs == nullptr) { return false; }
    RECT disp = clientRectOf(dlg, tabs);
    if (disp.right <= disp.left || disp.bottom <= disp.top) { return false; }
    ::SendMessageW(tabs, TCM_ADJUSTRECT, FALSE, reinterpret_cast<LPARAM>(&disp));
    RECT cli{};
    if (::GetClientRect(dlg, &cli) == FALSE) { return false; }
    *client = cli;
    page->left = disp.left;
    page->top = disp.top;
    page->right = std::min<LONG>(disp.right, cli.right);
    page->bottom = std::min<LONG>(disp.bottom, cli.bottom);
    return page->right > page->left && page->bottom > page->top;
}

void readHarnessState(HWND dlg, const std::vector<HWND>& all, int tab,
                      HarnessState* out) {
    KieeKeyProbeScrollState(dlg, &out->app);
    out->tab = tab;
    out->ctls.clear();
    out->deepestUnscrolledBottom = 0;
    out->visibleCount = 0;
    out->havePage = livePageRect(dlg, &out->page, &out->client);
    const RECT page = out->havePage ? out->page : RECT{0, 0, 0, 0};
    for (HWND child : all) {
        const int id = ::GetDlgCtrlID(child);
        if (KieeKeyProbeTabOfControl(dlg, id) != tab) { continue; }
        HarnessCtl c;
        c.hwnd = child;
        c.id = id;
        const RECT r = clientRectOf(dlg, child);
        c.x = r.left; c.y = r.top;
        c.w = static_cast<int>(r.right - r.left);
        c.h = static_cast<int>(r.bottom - r.top);
        c.shown = ::IsWindowVisible(child) != FALSE;
        {
            wchar_t cls[32];
            const int clsLen = ::GetClassNameW(child, cls, 32);
            const LONG_PTR style = ::GetWindowLongPtrW(child, GWL_STYLE);
            const bool isStatic = (clsLen == 6 && ::lstrcmpiW(cls, L"STATIC") == 0);
            const bool isButton = (clsLen == 6 && ::lstrcmpiW(cls, L"BUTTON") == 0);
            const bool isGroupBox = isButton && (style & BS_GROUPBOX) == BS_GROUPBOX;
            // v1.3.0-beta8fix1 (bug BS-22j): AND EVERY PAGE BUTTON. The solver
            // grows a page button to its own measured wrapped height (mkCtl gives
            // each of them BS_MULTILINE so Windows really draws the second line),
            // so the app's measurement is a contract for them too — that is
            // precisely the row the clip class was made of. Only page controls
            // reach this loop (the id tables decide), and only they carry the
            // bit, so the style IS the flag.
            c.growable = (isStatic && !isGroupBox &&
                          ((style & SS_TYPEMASK) != SS_ICON) &&
                          ((style & SS_TYPEMASK) != SS_OWNERDRAW)) ||
                         (isButton && (style & BS_MULTILINE) != 0);
        }
        HRGN rgn = ::CreateRectRgn(0, 0, 0, 0);
        if (rgn != nullptr) {
            const int type = ::GetWindowRgn(child, rgn);
            RECT box{};
            if (type != ERROR) {
                c.hasRegion = true;
                if (::GetRgnBox(rgn, &box) == NULLREGION) {
                    c.regionEmpty = true;
                    box = RECT{0, 0, 0, 0};
                }
                c.rl = box.left; c.rt = box.top; c.rr = box.right; c.rb = box.bottom;
            }
            ::DeleteObject(rgn);
        }
        const int rx = c.hasRegion ? c.x + c.rl : c.x;
        const int ry = c.hasRegion ? c.y + c.rt : c.y;
        const int rw = c.hasRegion ? (c.rr - c.rl) : c.w;
        const int rh = c.hasRegion ? (c.rb - c.rt) : c.h;
        const RECT eff = intersectRect(RECT{rx, ry, rx + rw, ry + rh}, page);
        c.ex = eff.left; c.ey = eff.top;
        c.ew = eff.right - eff.left; c.eh = eff.bottom - eff.top;
        if (c.shown && !rectEmpty(eff)) { ++out->visibleCount; }
        const int unscrolledBottom = c.y + c.h + out->app.offset;
        out->deepestUnscrolledBottom =
            std::max(out->deepestUnscrolledBottom, unscrolledBottom);
        out->ctls.push_back(c);
    }
}

std::string harnessStateStr(const HarnessState& s, const char* op, int step,
                            int seed, int fontPct) {
    const ProbeScrollStateT& a = s.app;
    return "step " + std::to_string(step) + " seed " + std::to_string(seed) +
           " op " + op + " tab " + std::to_string(s.tab) + " font " +
           std::to_string(fontPct) + "% dpi " + std::to_string(a.dpi) +
           " offset " + std::to_string(a.offset) + "/" + std::to_string(a.range) +
           " baseline " + (a.haveBaseline != 0 ? "yes(" + std::to_string(a.solvedCount) + ")"
                                               : std::string("NO")) +
           " latch " + (a.enabled != 0 ? "on" : "off") + "[" +
               (a.latchWriter != nullptr ? a.latchWriter : "?") + "#" +
               std::to_string(a.latchWrites) + "]" +
           " style " + (a.styleVScroll != 0 ? "VSCROLL" : "-") + "[" +
               (a.styleWriter != nullptr ? a.styleWriter : "?") + "#" +
               std::to_string(a.styleWrites) + "]" +
           (a.latchDrifts > 0 ? " latchDrifts " + std::to_string(a.latchDrifts) : "") +
           " info " + std::to_string(a.barPos) + "/" + std::to_string(a.barPage) +
           "/" + std::to_string(a.barMax) +
           " viewport " + rectStr(a.viewportX, a.viewportY, a.viewportW, a.viewportH) +
           " viewportBottom " + std::to_string(a.viewportBottom) +
           " page " + rectStr(s.page) +
           " ctls " + std::to_string(s.ctls.size()) +
           " visible " + std::to_string(s.visibleCount) +
           " contentBottom[" + std::to_string(s.tab) + "] " +
           std::to_string(a.contentBottom[s.tab]) +
           " stripShift " + std::to_string(a.stripShift[s.tab]) +
           " stripSeen " + std::to_string(a.stripSeen[s.tab]) +
           (a.stripRelayouts > 0 ? " stripRelayouts " + std::to_string(a.stripRelayouts)
                                 : std::string()) +
           " deepestUnscrolled " + std::to_string(s.deepestUnscrolledBottom);
}

// One invariant violation: a finding (so the probe exits non-zero and the CI
// digest shows the count) plus one line of state for ui_probe_trace.jsonl.
// v1.3.0-beta8fix1 (bug BS-19): the FIRST violating state of every invariant.
// The trace file has all of them, but the CI annotation only carries the digest
// line, and a count without a state is not a diagnosis: this keeps one example
// per invariant so the red run names the operation, the seed, the DPI, the
// offset and the page rectangle that produced it. Bounded (one per invariant,
// 420 chars) so the digest stays a line.
std::string g_invExample[14];
std::vector<std::string> g_handoverNotes;   // one per scale pass, for the digest
std::vector<std::string> g_passEntryNotes;  // what each pass started from

void harnessFail(int inv, std::vector<Finding>* findings, const std::string& why,
                 const std::string& state) {
    ++g_invFailures[inv];
    if (inv >= 0 && inv < 14 && g_invExample[inv].empty()) {
        std::string ex = why + " || " + state;
        if (ex.size() > 420) { ex.resize(417); ex += "..."; }
        g_invExample[inv] = std::move(ex);
    }
    ++g_fuzzFailures;
    const std::string kind = std::string("inv_I") + std::to_string(inv);
    findings->push_back({kind, why + " — " + state});
    harnessTrace("{\"invariant\": \"I" + std::to_string(inv) + "\", \"why\": \"" +
                 jsonEscape(why) + "\", \"state\": \"" + jsonEscape(state) + "\"}");
}

void harnessAssert(HWND dlg, const HarnessState& s, const char* op, int step,
                   int seed, int fontPct, std::vector<Finding>* findings) {
    const ProbeScrollStateT& a = s.app;
    const std::string state = harnessStateStr(s, op, step, seed, fontPct);
    const auto fail = [&](int inv, const std::string& why) {
        harnessFail(inv, findings, why, state);
    };
    const int tab = s.tab;
    const int contentBottom = a.contentBottom[tab];

    // v1.3.0-beta8fix1 (bug BS-18) — THE CONTRACT OF A DIALOG WITH NO LAYOUT.
    // The rescale drops the baseline and re-solves in the SAME call, so a
    // baseline-less dialog must never be observable. When it is (an invariant is
    // asserted after EVERY operation, so this is a state some operation left
    // behind), it may not claim a scroll state either: no range, no position, no
    // latch, no WS_VSCROLL. Everything that describes the dropped geometry goes
    // with it. That is the whole difference between "the page was just
    // re-laid-out" and the user's report: content gone, scrollbar gone, and every
    // wheel notch / arrow key / thumb drag a silent no-op because
    // applySettingsScrollOffset() has no rectangle to move.
    if (a.haveBaseline == 0) {
        ++g_invChecks[4];
        if (a.offset != 0 || a.range != 0 || a.enabled != 0 || a.styleVScroll != 0) {
            fail(4, "there is NO layout baseline but the scroll state survived: offset " +
                    std::to_string(a.offset) + " range " + std::to_string(a.range) +
                    " latch " + (a.enabled != 0 ? "on" : "off") + " style " +
                    (a.styleVScroll != 0 ? "VSCROLL" : "-") +
                    " — nothing can apply it and nothing can re-solve it");
        }
    }

    // I1 — the page must never be blank at the top of the scroll.
    if (a.haveBaseline != 0 && a.offset == 0 && !s.ctls.empty()) {
        ++g_invChecks[1];
        if (s.visibleCount == 0) {
            // Name the controls: "the page is empty" is a symptom, and the state
            // line alone cannot say whether the rows are BELOW the viewport, ABOVE
            // it, or clipped to nothing by a stale region. Three of them, with
            // their live rect and their region box, do.
            std::string who;
            for (const HarnessCtl& c : s.ctls) {
                if (who.size() > 300) { break; }
                who += std::string(who.empty() ? "" : " | ") + std::to_string(c.id) +
                       " " + rectStr(c.x, c.y, c.w, c.h) +
                       (c.hasRegion ? " r" + rectStr(c.rl, c.rt, c.rr - c.rl, c.rb - c.rt)
                                    : std::string(" r-")) +
                       (c.shown ? "" : " hidden");
            }
            fail(1, "page is EMPTY at offset 0 (page " + rectStr(s.page) + "): " +
                    (who.empty() ? std::string("no controls of this tab") : who));
        }
    }
    // I2 — nothing visible starts above the page it belongs to.
    if (s.havePage) {
        for (const HarnessCtl& c : s.ctls) {
            if (!c.shown || c.regionEmpty) { continue; }
            ++g_invChecks[2];
            const int top = c.hasRegion ? c.y + c.rt : c.y;
            if (top < s.page.top - 1) {
                fail(2, "id " + std::to_string(c.id) + " starts at y=" +
                        std::to_string(top) + ", above the page top " +
                        std::to_string(s.page.top));
                break;
            }
        }
    }
    // I3 — the stored content depth must agree with the app's own rectangles.
    // The current tab only: another tab's stored depth is measured against the
    // page rectangle of ITS solve, and a row-level difference to the live one is
    // the tab strip, not a defect. For the tab on screen the app's own deepest
    // control is the truth.
    if (a.haveBaseline != 0 && s.havePage && !s.ctls.empty()) {
        ++g_invChecks[3];
        if (contentBottom < s.page.top) {
            fail(3, "contentBottom[" + std::to_string(tab) + "]=" +
                    std::to_string(contentBottom) + " is above the page top " +
                    std::to_string(s.page.top));
        }
    }
    ++g_invChecks[3];
    if (s.deepestUnscrolledBottom > contentBottom + 1) {
        fail(3, "contentBottom[" + std::to_string(tab) + "]=" +
                std::to_string(contentBottom) + " but the tab's own deepest control "
                "reaches " + std::to_string(s.deepestUnscrolledBottom) +
                (a.haveBaseline == 0 ? " (and there is NO layout baseline)" : ""));
    }
    // I4 — the range is RECOMPUTED, never latched.
    if (a.haveBaseline != 0) {
        ++g_invChecks[4];
        const int wantRange = std::max(0, contentBottom - a.viewportBottom);
        if (a.range != wantRange) {
            fail(4, "range " + std::to_string(a.range) + " != contentBottom " +
                    std::to_string(contentBottom) + " - viewportBottom " +
                    std::to_string(a.viewportBottom) + " = " + std::to_string(wantRange));
        }
    }
    // I5 — the latch, the style bit and Win32's own enable rule agree.
    ++g_invChecks[5];
    if ((a.enabled != 0) != (a.styleVScroll != 0)) {
        // v1.3.0-beta8fix1 (bug BS-22c): name the writer of each half. The pair is
        // written by ONE function (settingsApplyScrollbarLatch) and re-decided by
        // settingsSyncScrollbarLatch; the tags say which call wrote each side last
        // and how many times, so a divergence that survives the ownership fix
        // points at the operation that broke it instead of costing a CI round.
        fail(5, std::string("the app's bar latch says ") +
                (a.enabled != 0 ? "on" : "off") + " (last write " +
                (a.latchWriter != nullptr ? a.latchWriter : "?") + " #" +
                std::to_string(a.latchWrites) + ") but WS_VSCROLL is " +
                (a.styleVScroll != 0 ? "set" : "clear") + " (last write " +
                (a.styleWriter != nullptr ? a.styleWriter : "?") + " #" +
                std::to_string(a.styleWrites) + ") — the two halves of one answer " +
                "were written apart; the sync found this " +
                std::to_string(a.latchDrifts) + " time(s)");
    }
    {
        ++g_invChecks[5];
        const bool winUsable = a.barPage < a.barMax + 1;
        // The ruler is the CURRENT TAB's depth, because that is what the range
        // Win32 is answering about is made of: settingsScrollSetTab() sets
        // nMax/nPage from perTabContentBottom[tabIndex] — one range, for the tab
        // on screen. The WS_VSCROLL STYLE bit is the all-tabs decision (a dialog
        // whose bar appears and disappears as the user walks the tabs would move
        // every row up and down under them), and the first half of this check
        // owns that bit. Reading the "usable" answer against all nine depths made
        // the two halves contradict each other and I4 as well (I4 verifies
        // `range == contentBottom[tab] - viewportBottom`, i.e. the current tab):
        // any state with a shallow tab on screen while a deeper tab was in the
        // dialog was reported as a dead bar that "should" be alive — 309 of the
        // 55c965c..76f955a run's violations, every one of them a state whose own
        // range was 0 because its own page fitted.
        bool needBar = (a.contentBottom[tab] > s.page.bottom);
        if (winUsable != needBar) {
            fail(5, std::string("Win32 says the bar is ") +
                    (winUsable ? "usable" : "dead") + " (page " +
                    std::to_string(a.barPage) + " max " + std::to_string(a.barMax) +
                    ") but tab " + std::to_string(tab) + "'s content (depth " +
                    std::to_string(a.contentBottom[tab]) + ", page bottom " +
                    std::to_string(s.page.bottom) + ") " +
                    (needBar ? "does need it" : "does not"));
        }
    }
    // I6 — content inside the app's own viewport is never clipped to nothing.
    if (a.haveBaseline != 0 && s.havePage && a.viewportW > 0 && a.viewportH > 0) {
        const RECT vp{a.viewportX, a.viewportY, a.viewportX + a.viewportW,
                      a.viewportY + a.viewportH};
        const RECT truth = intersectRect(vp, s.page);
        if (!rectEmpty(truth)) {
            for (const HarnessCtl& c : s.ctls) {
                if (!c.shown || !c.regionEmpty) { continue; }
                ++g_invChecks[6];
                const RECT live{c.x, c.y, c.x + c.w, c.y + c.h};
                if (!rectEmpty(intersectRect(live, truth))) {
                    fail(6, "id " + std::to_string(c.id) + " is inside the viewport " +
                            rectStr(truth) + " but its window region is EMPTY "
                            "(rect " + rectStr(live) + ")");
                    break;
                }
            }
        }
    }
    // I6 (second half, v1.3.0-beta8fix1 bug BS-22k) — AND THE WINDOW IS THE
    // RECTANGLE THE SOLVER ASKED FOR.
    //
    // A layout is only as good as the windows it was applied to. The x64 run
    // 35974677491 measured the case: `id 625 (ComboBox) 220,415 398x40 and id 627
    // (Static) 55,449 583x121 overlap by 398x6 px solved 220,675 398x31` — the
    // plan put the combo at 31 px, the window it really has is 40 px, and the row
    // placed against the plan's 31 px is covered by the combo's real 9 px. Win32
    // decides a combo box's height (its item height plus borders), so the plan and
    // the window can disagree after the solve that applied them, whatever caused
    // it. This check names the control and both rectangles in every state, so the
    // next round has the mechanism instead of a 6 px overlap.
    for (const HarnessCtl& c : s.ctls) {
        if (!c.shown || c.id == 0) { continue; }
        int solved[4] = {0, 0, 0, 0};
        if (KieeKeyProbeSolvedRect(dlg, c.id, solved) == 0 || solved[2] <= 0) { continue; }
        ++g_invChecks[6];
        const int dw = c.w - solved[2];
        const int dh = c.h - solved[3];
        if (dw > 1 || dw < -1 || dh > 1 || dh < -1) {
            fail(6, "id " + std::to_string(c.id) + " lives " + rectStr(c.x, c.y, c.w, c.h) +
                    " but the solver's baseline is " +
                    rectStr(solved[0], solved[1], solved[2], solved[3]) + " (" +
                    std::to_string(dw) + " px wider, " + std::to_string(dh) +
                    " px taller) — the plan and the window disagree, so every row "
                    "below is placed against a size this window does not have");
            break;
        }
    }
    // I7 — at offset 0 a control fully inside the viewport carries no region.
    if (a.haveBaseline != 0 && a.offset == 0 && s.havePage) {
        const RECT vp{a.viewportX, a.viewportY, a.viewportX + a.viewportW,
                      a.viewportY + a.viewportH};
        const RECT truth = intersectRect(vp, s.page);
        if (!rectEmpty(truth)) {
            for (const HarnessCtl& c : s.ctls) {
                if (!c.shown || !c.hasRegion) { continue; }
                const bool inside = c.x >= truth.left && c.y >= truth.top &&
                                    c.x + c.w <= truth.right && c.y + c.h <= truth.bottom;
                if (!inside) { continue; }
                ++g_invChecks[7];
                fail(7, "id " + std::to_string(c.id) + " sits fully inside the viewport " +
                        rectStr(truth) + " at offset 0 but still carries a window region");
                break;
            }
        }
    }
    // I12 — the app's own measurement must fit the box it was applied to.
    //
    // v1.3.0-beta8fix1 (bug BS-22c): AND ONLY THE CONTROLS THE APP MEASURES.
    //
    // `KieeKeyProbeMeasureStaticHeight` is the app's solver measurement
    // (`measureStaticTextHeightPx`: DrawTextW + DT_CALCRECT | DT_WORDBREAK), and
    // the solver applies it to ONE class of control — `spec.growable`:
    // a STATIC that is not a group box, not SS_ICON and not SS_OWNERDRAW
    // (src/app/main.cpp, the build loop; `requiredHeight` is set there and nowhere
    // else, and `autoFit` grows exactly those boxes) — or (v1.3.0-beta8fix1, bug
    // BS-22j) a PAGE BUTTON, which carries BS_MULTILINE and is grown to the same
    // measurement. Asking any other control the same question measures a rendering
    // it never performs: an EDIT or a chrome button is SINGLE-LINE, so a
    // wrapped height of 64 px for a 33 px box describes two lines Win32 never
    // draws — and the check fired 1313 times in the 77e8fea run on exactly that
    // arithmetic (`id 591 needs 64px but its box is 33px tall`, id 591 being
    // IDC_CHK_CHAOS_MASTER, a check box whose label is simply wider than its box).
    // The defect behind it is real and has its own owner: `checkTextFit` reports it
    // as `clip` (and the solver now fits such a row's width where the page has
    // room). What this invariant owns is the solver's own contract: a box grown to
    // `requiredHeight` must hold that measurement. Controls outside the contract
    // are still EXAMINED (the check counter below counts every one of them, so the
    // coverage stays visible) — they are just not judged by a rendering rule that
    // does not apply to them.
    if (s.havePage) {
        int judged = 0;
        for (const HarnessCtl& c : s.ctls) {
            if (judged >= 8) { break; }
            if (!c.shown || c.regionEmpty || c.ew <= 0 || c.eh <= 0) { continue; }
            if (c.h <= 0) { continue; }
            ++g_invChecks[12];          // every control examined, contract or not
            if (!c.growable) { continue; }
            const int need = KieeKeyProbeMeasureStaticHeight(dlg, c.id);
            if (need <= 0) { continue; }
            ++judged;
            if (need > c.h + 2) {
                fail(12, "id " + std::to_string(c.id) + " needs " +
                        std::to_string(need) + "px but its box is " +
                        std::to_string(c.h) + "px tall — the solver's own "
                        "measurement does not fit the box it grew");
                break;
            }
        }
    }
}

// The operation set. Every one of these is a path the app itself runs.
enum HarnessOp {
    kOpScrollDown, kOpWheelDown, kOpScrollTop, kOpScrollBottom, kOpSelectTab,
    kOpReflow, kOpTick, kOpDpi, kOpDisplayChange, kOpResize, kOpFontScale,
    kOpTypeRow, kOpWheelUp, kOpReselectTab, kOpCount
};

const char* harnessOpName(int op) {
    switch (op) {
        case kOpScrollDown:    return "scroll_down";
        case kOpWheelDown:     return "wheel_down";
        case kOpScrollTop:     return "scroll_top";
        case kOpScrollBottom:  return "scroll_bottom";
        case kOpSelectTab:     return "select_tab";
        case kOpReflow:        return "reflow";
        case kOpTick:          return "tick";
        case kOpDpi:           return "dpi_change";
        case kOpDisplayChange: return "display_change";
        case kOpResize:        return "resize";
        case kOpFontScale:     return "font_scale";
        case kOpTypeRow:       return "type_row";
        case kOpWheelUp:       return "wheel_up";
        case kOpReselectTab:   return "reselect_tab";
        default:               return "?";
    }
}

// The app's live rows (the 500 ms tick rewrites each of them). Typing a longer
// text into one is the app's own growth request, and the tick that follows
// consumes the pending reflow and puts the app's own text back.
int liveRowForTab(HWND dlg, int tab) {
    static const int kRows[] = {IDC_STAT_INFO_STATUS, IDC_STAT_ARCADE_STATUS,
                                IDC_STAT_AI_STATS, IDC_STAT_COACH_ADVICE};
    for (int id : kRows) {
        if (KieeKeyProbeTabOfControl(dlg, id) == tab) { return id; }
    }
    return 0;
}

std::vector<HWND> stableChildren(HWND dlg) {
    std::vector<HWND> all;
    for (HWND c = ::GetWindow(dlg, GW_CHILD); c != nullptr;
         c = ::GetWindow(c, GW_HWNDNEXT)) {
        all.push_back(c);
    }
    return all;
}

void sendWheel(HWND dlg, int notches, const RECT& page) {
    if (notches == 0) { return; }
    POINT pt{page.left + (page.right - page.left) / 2,
             page.top + (page.bottom - page.top) / 2};
    ::ClientToScreen(dlg, &pt);
    const int delta = -WHEEL_DELTA * notches;
    ::SendMessageW(dlg, WM_MOUSEWHEEL,
                   MAKEWPARAM(0, static_cast<WORD>(static_cast<short>(delta))),
                   MAKELPARAM(pt.x, pt.y));
}

// Run one operation from the app's own paths, then assert every invariant.
void harnessStep(HWND dlg, const std::vector<HWND>& all, int tabCount, int* curTab,
                 int* fontPct, unsigned nativeDpi, const RECT& origClient,
                 HarnessRng& rng, int op, int step, int seed,
                 std::vector<Finding>* findings) {
    (void)nativeDpi;
    const char* opName = harnessOpName(op);
    switch (op) {
        case kOpScrollDown: {
            ProbeScrollStateT st{};
            KieeKeyProbeScrollState(dlg, &st);
            KieeKeyProbeSetOffset(dlg, st.offset + rng.pick(8, 120));
            break;
        }
        case kOpWheelDown: {
            HarnessState s;
            readHarnessState(dlg, all, *curTab, &s);
            sendWheel(dlg, rng.pick(1, 3), s.havePage ? s.page : RECT{0, 0, 40, 40});
            break;
        }
        case kOpWheelUp: {
            HarnessState s;
            readHarnessState(dlg, all, *curTab, &s);
            sendWheel(dlg, -rng.pick(1, 3), s.havePage ? s.page : RECT{0, 0, 40, 40});
            break;
        }
        case kOpScrollTop:
            ::SendMessageW(dlg, WM_VSCROLL, MAKEWPARAM(SB_TOP, 0), 0);
            break;
        case kOpScrollBottom:
            ::SendMessageW(dlg, WM_VSCROLL, MAKEWPARAM(SB_BOTTOM, 0), 0);
            break;
        case kOpSelectTab:
        case kOpReselectTab:
            *curTab = (op == kOpSelectTab) ? rng.pick(0, tabCount - 1) : *curTab;
            KieeKeyProbeSelectTab(dlg, *curTab);
            break;
        case kOpReflow: {
            HarnessState before;
            readHarnessState(dlg, all, *curTab, &before);
            KieeKeyProbeReflowNow(dlg);
            HarnessState after;
            readHarnessState(dlg, all, *curTab, &after);
            // I9 — a reflow may grow the page, never move it up/sideways and
            // never make it shallower.
            ++g_invChecks[9];
            if (after.deepestUnscrolledBottom < before.deepestUnscrolledBottom - 1) {
                harnessFail(9, findings,
                            "the reflow made the page shallower (" +
                                std::to_string(before.deepestUnscrolledBottom) +
                                " -> " + std::to_string(after.deepestUnscrolledBottom) + ")",
                            harnessStateStr(after, opName, step, seed, *fontPct));
            } else {
                for (std::size_t i = 0; i < after.ctls.size(); ++i) {
                    const HarnessCtl& b = before.ctls[i];
                    const HarnessCtl& a = after.ctls[i];
                    if (b.hwnd != a.hwnd) { continue; }
                    if (a.x < b.x - 1 || (a.y + after.app.offset) <
                                         (b.y + before.app.offset) - 1) {
                        harnessFail(9, findings,
                                    "the reflow moved id " + std::to_string(a.id) +
                                        " up/sideways (" + rectStr(b.x, b.y, b.w, b.h) +
                                        " -> " + rectStr(a.x, a.y, a.w, a.h) + ")",
                                    harnessStateStr(after, opName, step, seed, *fontPct));
                        break;
                    }
                }
            }
            break;
        }
        case kOpTick:
            ::SendMessageW(dlg, WM_TIMER, 1, 0);
            break;
        case kOpDpi: {
            static const UINT kDpis[] = {96U, 120U, 144U, 192U};
            const UINT d = kDpis[static_cast<std::size_t>(rng.pick(0, 3))];
            KieeKeyProbeSimulateDpi(dlg, d);
            *fontPct = 100;   // the rescale re-mints the app's own fonts
            break;
        }
        case kOpDisplayChange:
            // The app's own response to a monitor topology / resolution / scale
            // change. The runner's desktop cannot change its DPI, so the scale
            // mismatch the user's machine produces (the app solved at one scale,
            // the monitor now reports another) is driven by the harness.
            KieeKeyProbeSetWindowDpiOverride(nativeDpi != 144U ? 144U : 120U);
            KieeKeyProbeDisplayChange(dlg);
            KieeKeyProbeSetWindowDpiOverride(0);
            *fontPct = 100;
            break;
        case kOpResize: {
            const int w = std::max(320, ::MulDiv(origClient.right, rng.pick(70, 130), 100));
            const int h = std::max(240, ::MulDiv(origClient.bottom, rng.pick(70, 130), 100));
            KieeKeyProbeResize(dlg, w, h);
            // The dialog has no WM_SIZE handler: what a work-area change ends in
            // is the app's own re-solve.
            KieeKeyProbeReflowNow(dlg);
            break;
        }
        case kOpFontScale: {
            static const int kScales[] = {100, 125, 150};
            *fontPct = kScales[static_cast<std::size_t>(rng.pick(0, 2))];
            KieeKeyProbeFontScale(dlg, *fontPct);
            break;
        }
        case kOpTypeRow: {
            const int id = liveRowForTab(dlg, *curTab);
            if (id != 0) {
                std::wstring text;
                for (int i = 0; i < 6; ++i) {
                    text += L"Dòng chẩn đoán mở rộng ";
                    text += std::to_wstring(i + 1);
                    text += L": bộ gõ vẫn đang chạy bình thường.\r\n";
                }
                KieeKeyProbeTypeRow(dlg, id, text.c_str());
                ::SendMessageW(dlg, WM_TIMER, 1, 0);   // consume the pending reflow
            }
            break;
        }
        default: break;
    }

    HarnessState s;
    readHarnessState(dlg, all, *curTab, &s);
    ++g_fuzzChecks;
    harnessAssert(dlg, s, opName, step, seed, *fontPct, findings);
}

// The named, deterministic scenario: what a display change does to a dialog the
// user is reading mid-scroll. It is the sequence the four green rounds never
// drove, and it is the one the user's report is about.
int harnessScenario(HWND dlg, const std::vector<HWND>& all, int tabCount,
                    unsigned nativeDpi, const RECT& origClient,
                    std::vector<Finding>* findings) {
    ++g_scenarioRuns;
    const int before = g_fuzzFailures;

    ProbeScrollStateT st{};
    KieeKeyProbeScrollState(dlg, &st);
    int deepestTab = 0;
    for (int t = 1; t < tabCount; ++t) {
        if (st.contentBottom[t] > st.contentBottom[deepestTab]) { deepestTab = t; }
    }
    KieeKeyProbeSelectTab(dlg, deepestTab);
    KieeKeyProbeScrollState(dlg, &st);
    KieeKeyProbeSetOffset(dlg, st.range > 1 ? st.range / 2 : 1);
    KieeKeyProbeSimulateDpi(dlg, nativeDpi);      // the app believes the runner's scale

    KieeKeyProbeSetWindowDpiOverride(nativeDpi != 144U ? 144U : 120U);
    KieeKeyProbeDisplayChange(dlg);               // the monitor now reports 150 %
    {
        HarnessState s;
        readHarnessState(dlg, all, deepestTab, &s);
        ++g_fuzzChecks;
        harnessAssert(dlg, s, "scenario_display_change_mid_scroll", 0, 0, 100, findings);
    }
    // The app's own recovery paths: a reflow, a return to the top, a tab switch.
    KieeKeyProbeReflowNow(dlg);
    KieeKeyProbeSetOffset(dlg, 0);
    KieeKeyProbeSelectTab(dlg, deepestTab);
    KieeKeyProbeSetWindowDpiOverride(0);
    {
        HarnessState s;
        readHarnessState(dlg, all, deepestTab, &s);
        ++g_fuzzChecks;
        harnessAssert(dlg, s, "scenario_after_recovery_ops", 0, 0, 100, findings);
    }

    // I8/I11 — the real screen, not a WM_PRINTCLIENT render: a forced full
    // repaint must not change one pixel, and the page must paint content.
    std::vector<std::uint32_t> beforePx, afterPx;
    int w = 0, h = 0;
    if (readScreenClient(dlg, &beforePx, &w, &h) && screenIsUsable(beforePx)) {
        ::RedrawWindow(dlg, nullptr, nullptr,
                       RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW |
                           RDW_FRAME);
        ::Sleep(30);
        if (readScreenClient(dlg, &afterPx, &w, &h) &&
            afterPx.size() == beforePx.size()) {
            ++g_invChecks[8];
            int diff = 0;
            for (std::size_t i = 0; i < beforePx.size(); ++i) {
                if (beforePx[i] != afterPx[i]) { ++diff; }
            }
            if (diff != 0) {
                harnessFail(8, findings,
                            "a forced full repaint changed " + std::to_string(diff) +
                                " client pixels (stale frame)",
                            "scenario_forced_repaint dpi " + std::to_string(st.dpi) +
                                " client " + std::to_string(w) + "x" + std::to_string(h));
            }
        }
        // I11: sample the middle row of up to 8 visible page controls; the page
        // background is the pixel just outside the page rect.
        HarnessState s;
        readHarnessState(dlg, all, deepestTab, &s);
        if (s.havePage && w > 0 && h > 0) {
            const std::uint32_t bg =
                beforePx[static_cast<std::size_t>(2) * static_cast<std::size_t>(w) + 2];
            // v1.3.0-beta8fix1 (bug BS-22g): A CONTROL THE CAPTURE DOES NOT COVER
            // IS NOT EVIDENCE OF A BLANK PAGE. The sample points are skipped when
            // they fall outside the captured client, and the old counter judged
            // such a control anyway — three visible-but-largely-off-screen controls
            // were enough to report `the page paints background only` about a page
            // the capture had never looked at. A control counts as judged only when
            // at least one of its three middle-row samples is inside the capture,
            // and a page with fewer than three of those is reported as its own
            // finding (with the numbers) instead of passing as "nothing to see":
            // an empty page is the user's report, so a green here may not be a
            // measurement that never ran.
            int judged = 0;
            int painted = 0;
            int uncovered = 0;
            std::vector<POINT> samples;   // every sample the capture really covers
            std::string evidence;
            for (const HarnessCtl& c : s.ctls) {
                if (judged >= 8) { break; }
                if (!c.shown || c.regionEmpty || c.ew < 24 || c.eh < 8) { continue; }
                const int y = c.ey + c.eh / 2;
                int sampled = 0;
                int differs = 0;
                for (int k = 1; k <= 3; ++k) {
                    const int x = c.ex + c.ew * k / 4;
                    if (x < 0 || y < 0 || x >= w || y >= h) { continue; }
                    ++sampled;
                    const std::uint32_t px =
                        beforePx[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                                 static_cast<std::size_t>(x)];
                    if (px != bg) { ++differs; }
                }
                if (sampled == 0) { ++uncovered; continue; }
                ++judged;
                for (int k = 1; k <= 3; ++k) {
                    const int x = c.ex + c.ew * k / 4;
                    if (x < 0 || y < 0 || x >= w || y >= h) { continue; }
                    samples.push_back(POINT{x, y});
                }
                if (differs > 0) { ++painted; }
                if (evidence.size() < 220) {
                    evidence += " " + std::to_string(c.id) + " " +
                                rectStr(c.ex, c.ey, c.ew, c.eh) + " painted " +
                                std::to_string(differs) + "/" + std::to_string(sampled);
                }
            }
            // v1.3.0-beta8h: and the SAME capture is read on the always-visible
            // chrome. The page and the chrome are drawn by the same window, so a
            // capture that has the chrome and not the page is a page that was never
            // painted (the user's report), while a capture with neither is not this
            // window's frame at all — an occluded or off-screen dialog, where a
            // finding about the page would be a measurement of somebody else's
            // pixels. The render cross-check above says what the app WOULD draw.
            int chromeJudged = 0;
            int chromePainted = 0;
            for (const HarnessCtl& c : s.ctls) {
                if (chromeJudged >= 4) { break; }
                if (!c.shown || c.regionEmpty || c.ew < 24 || c.eh < 8) { continue; }
                if (KieeKeyProbeTabOfControl(dlg, c.id) >= 0) { continue; }
                const int y = c.ey + c.eh / 2;
                int sampled = 0;
                int differs = 0;
                for (int k = 1; k <= 3; ++k) {
                    const int x = c.ex + c.ew * k / 4;
                    if (x < 0 || y < 0 || x >= w || y >= h) { continue; }
                    ++sampled;
                    const std::uint32_t px =
                        beforePx[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                                 static_cast<std::size_t>(x)];
                    if (px != bg) { ++differs; }
                }
                if (sampled == 0) { continue; }
                ++chromeJudged;
                if (differs > 0) { ++chromePainted; }
            }
            const std::string ground =
                "scenario_screen_paint tab " + std::to_string(deepestTab) + " client " +
                std::to_string(w) + "x" + std::to_string(h) + " app dpi " +
                std::to_string(s.app.dpi) + " page " + rectStr(s.page) + " offset " +
                std::to_string(s.app.offset) + " strip " +
                std::to_string(s.app.stripShift[deepestTab]) + "/" +
                std::to_string(s.app.stripSeen[deepestTab]) + " rows " +
                std::to_string(s.app.stripRows) + " judged " + std::to_string(judged) +
                " uncovered " + std::to_string(uncovered) + " rendered " +
                std::to_string(renderPaintCountAt(dlg, samples, bg)) + "/" +
                std::to_string(samples.size()) + " chrome " +
                std::to_string(chromePainted) + "/" + std::to_string(chromeJudged) +
                evidence;
            ++g_invChecks[11];
            if (judged >= 3 && painted == 0) {
                harnessFail(11, findings,
                            "the page paints background only: " + std::to_string(judged) +
                                " visible controls, none of their middle rows differs "
                                "from the page background",
                            ground);
            } else if (judged < 3) {
                harnessFail(11, findings,
                            "the screen capture covers fewer than three whole page "
                            "controls (" + std::to_string(judged) + " covered, " +
                                std::to_string(uncovered) + " not covered by the "
                                "capture) — the page's content cannot be judged from "
                                "this frame",
                            ground);
            }
        }
    } else {
        ++g_screenUnavailable;
    }

    // Leave the dialog as the app's own state machine wants it: the runner's
    // scale, the original client size, the first tab, the top of the page.
    KieeKeyProbeFontScale(dlg, 100);
    KieeKeyProbeResize(dlg, static_cast<int>(origClient.right),
                       static_cast<int>(origClient.bottom));
    KieeKeyProbeReflowNow(dlg);
    KieeKeyProbeSetOffset(dlg, 0);
    KieeKeyProbeSelectTab(dlg, 0);

    const int failures = g_fuzzFailures - before;
    if (failures > 0) { ++g_scenarioFailures; }
    harnessTrace("{\"scenario\": \"display_change_mid_scroll\", \"tab\": " +
                 std::to_string(deepestTab) + ", \"dpi\": " + std::to_string(st.dpi) +
                 ", \"violations\": " + std::to_string(failures) + "}");
    return failures;
}

// The seeded fuzz pass: fixed seeds, a fixed number of steps per tab and scale,
// every step asserting the invariants. The step budget is in the JSON counters,
// so "0 findings" can never mean "it never ran".
//===========================================================================
// v1.3.0-beta8fix1 (bug BS-21) — THE TAB STRIP CHANGES SHAPE, THE PAGE COMES
// BACK.
//
// The display rectangle the page lives in starts where the tab strip ends, and
// the strip is planned from the label widths: a narrow dialog (or a bigger font,
// or a higher scale) wraps the nine labels onto two or three rows and the page
// top moves DOWN. BS-10 taught the solver to move the page content down with it
// so nothing hides under the labels — but the move is computed from the
// BASELINE, which already carries the previous move, so it only ever grows. When
// the strip fits one row again nothing pulls the page back up: the content stays
// as far below the page as the widest strip ever pushed it.
//
// This scenario drives exactly that transition, on the app's own paths, and
// measures the drift instead of asserting the symptom: for every tab, one row ->
// multi-row -> one row, three times, at the pass's own scale. The invariants
// (harnessAssert) cover the ordinary "is the page coherent" question; the checks
// below cover the transition:
//
//   * the page top returns to its one-row value when the strip does;
//   * the tab's first control returns to its one-row y (the content is not left
//     below the page: `stripShift` must come back to 0);
//   * repeating the cycle does not accumulate (cycle 3 == cycle 1).
//
// 639x176-style constrained pages are the point: a short client whose strip has
// wrapped has almost no room, so a stale downward shift is not a cosmetic offset,
// it is an empty page — the state the fuzz reached with 20 controls parked at
// y=383..1593 under a 176 px viewport.
//===========================================================================
// The state description every strip-cycle finding quotes: tab, cycle, the pass's
// scale, the client the cycle runs at and the font scale the labels are read at.
std::string whereOf(const HarnessState& s, int tab, unsigned passDpi, int cycle,
                    int step, int wideW, int clientH, int fontPct) {
    return "tab " + std::to_string(tab) + " cycle " + std::to_string(cycle) +
           " pass dpi " + std::to_string(passDpi) + " client " +
           std::to_string(wideW) + "x" + std::to_string(clientH) +
           " font " + std::to_string(fontPct) + "% page top " +
           std::to_string(s.page.top) + " strip " +
           std::to_string(s.app.stripShift[tab]) + "/" +
           std::to_string(s.app.stripSeen[tab]) + " rows " +
           std::to_string(s.app.stripRows) + " step " + std::to_string(step);
}

int harnessScenarioStripCycles(HWND dlg, const std::vector<HWND>& all, int tabCount,
                               unsigned passDpi, unsigned nativeDpi,
                               const RECT& origClient,
                               std::vector<Finding>* findings) {
    ++g_scenarioRuns;
    const int before = g_fuzzFailures;
    const int wideW = static_cast<int>(origClient.right);
    const int clientH = static_cast<int>(origClient.bottom);
    // v1.3.0-beta8fix1 (bug BS-22c): THE WRAP IS DRIVEN BY THE TEXT SCALE, NOT BY
    // A FIXED WIDTH. The first version of this scenario narrowed the client to 55 %
    // to make the nine tab labels wrap, which is a property of the labels' font: at
    // 96 dpi they wrapped, at 125 %/150 % (labels 1.25x/1.5x wider, and a smaller
    // client to wrap into) they did not always — and a cycle that never reaches the
    // transition proves nothing about the transition. The harness's own text-scale
    // path (KieeKeyProbeFontScale, the app's real font factory + re-solve) makes the
    // labels 1.5x wider at EVERY scale, so the strip wraps and then fits one row
    // again by construction; the check below still proves the wrap happened.
    const int kWrapFontPct = 150;

    // Start from the pass's own state: its scale, one-row strip, top of the page.
    //
    // v1.3.0-beta8fix1 (bug BS-22g): AND THE CLIENT HAS TO BE BIG ENOUGH TO HOLD
    // ONE ROW AT THIS SCALE, or the scenario starts from a strip that is already
    // wrapped and its "the labels grew and the strip wrapped" measurement is
    // really "the labels were already too wide". At the 120 dpi pass of the
    // 904334c x64 run the client was the pass's scaled 683 px, the labels needed
    // more than the 645 px tab that leaves, and every cycle reported
    // `[I1] the nine tab labels did not wrap at font 150% (page top 161 vs the
    // one-row 161, stripShift seen 29 px)` — 54 findings for a transition that HAD
    // happened (the shift is the app's own record of the strip pushing the page).
    // The width is the pass client scaled to this dpi plus 15 % of slack, and the
    // cycle then PROVES its precondition (stripRows == 1) instead of assuming it.
    // ...and the client is CALIBRATED to this pass, because both halves of the
    // transition have to be reachable at once: one row at 100 % (or the cycle
    // starts inside a wrap it did not cause) and more than one at 150 % (or the
    // cycle has no transition to measure). The pass client alone satisfies both
    // only at 96 dpi: at 120 dpi it already wraps, and a client widened to fix
    // that stops wrapping at 150 % (the 904334c run had 54 findings of the first
    // kind, the eb68391 run 81 of the second, `rows 1`, `stripShift seen 0 px`).
    // Six bounded steps, each one asking the APP's own plan (stripRows), and the
    // per-tab cycle still proves both halves for every tab.
    const int stripW0 = std::max(wideW, ::MulDiv(wideW, static_cast<int>(passDpi), 96));
    int stripW = stripW0;
    bool measurable = false;
    KieeKeyProbeSimulateDpi(dlg, passDpi);
    KieeKeyProbeFontScale(dlg, 100);
    for (int attempt = 0; attempt < 6; ++attempt) {
        KieeKeyProbeResize(dlg, stripW, clientH);
        KieeKeyProbeReflowNow(dlg);
        KieeKeyProbeSetOffset(dlg, 0);
        HarnessState probeState;
        readHarnessState(dlg, all, 0, &probeState);
        const int shiftBefore = probeState.app.stripShift[0];
        KieeKeyProbeFontScale(dlg, kWrapFontPct);
        KieeKeyProbeReflowNow(dlg);
        readHarnessState(dlg, all, 0, &probeState);
        const bool grows = probeState.app.stripShift[0] > shiftBefore;
        KieeKeyProbeFontScale(dlg, 100);
        if (grows) { measurable = true; break; }
        // At this width the labels fit the tab either way, so there is no
        // transition to measure: try a narrower client.
        stripW = std::max(stripW0 * 2 / 3, stripW * 4 / 5);
    }
    KieeKeyProbeResize(dlg, stripW, clientH);
    KieeKeyProbeReflowNow(dlg);
    KieeKeyProbeSetOffset(dlg, 0);

    // v1.3.0-beta8fix1 (bug BS-22i): A PASS THAT CANNOT GROW THE STRIP REPORTS
    // ITSELF. At 120/144 dpi the runner's screen (768 px tall) cannot give the
    // window the height the scale needs: the tab control is capped by
    // tabHeightForClient so the bottom row stays visible, its display rectangle
    // stops moving, and the app's recorded strip height stays at whatever the
    // wrapped-at-100 % state was (measured: `strip shift 29 -> 29 px` at every
    // width down to the window's own minimum). That is a property of the screen,
    // not of the dialog — the app's own state in those passes is asserted in full
    // by the invariant battery below (I2..I12 on the wrapped and returned states),
    // and the grow/shrink transition itself is measured at the runner's real scale,
    // where it MUST be measurable: a native pass that cannot grow the strip is a
    // finding, so this can never become the reason the transition stops being
    // tested.
    if (!measurable) {
        ++g_stripCycleUnavailable;
        HarnessState st;
        readHarnessState(dlg, all, 0, &st);
        const std::string note =
            "strip-cycle not measurable: pass dpi " + std::to_string(passDpi) +
            " client " + std::to_string(stripW) + "x" + std::to_string(clientH) +
            " page " + rectStr(st.page) + " strip shift " +
            std::to_string(st.app.stripShift[0]) + " px rows " +
            std::to_string(st.app.stripRows) +
            " — the window cannot be made tall enough at this scale for the strip "
            "to gain a row (the tab control's height is capped so the bottom row "
            "stays visible)";
        g_stripCycleNote = note;
        harnessTrace("{\"scenario\": \"strip_cycles\", \"measured\": false, \"note\": \"" +
                     note + "\"}");
        if (passDpi == nativeDpi) {
            harnessFail(1, findings,
                        "the strip's grow/shrink transition is not measurable even at "
                        "the runner's own scale (" + note + ") — the native pass is "
                        "where this dialog's strip transition is proven",
                        whereOf(st, 0, passDpi, 0, 0, stripW, clientH, 100));
        }
    }

    for (int tab = 0; tab < tabCount; ++tab) {
        if (KieeKeyProbeSelectTab(dlg, tab) != 0) { break; }
        ::Sleep(15);

        // --- the one-row reference -------------------------------------------
        KieeKeyProbeSetOffset(dlg, 0);
        KieeKeyProbeReflowNow(dlg);
        HarnessState base;
        readHarnessState(dlg, all, tab, &base);
        int baseFirstY = -1;
        int baseFirstId = 0;
        for (const HarnessCtl& c : base.ctls) {
            if (!c.shown || c.regionEmpty) { continue; }
            if (baseFirstY < 0 || c.y < baseFirstY) { baseFirstY = c.y; baseFirstId = c.id; }
        }
        const int basePageTop = base.page.top;
        const bool multiRowPossible = base.havePage && basePageTop > 0;

        if (!multiRowPossible || baseFirstY < 0) { continue; }

        // v1.3.0-beta8fix1 (bug BS-22h): the strip's DISPLAYED height is the shift
        // the solver recorded from the tab control's display rectangle, not the
        // row count the planner estimated: the plan can ask for TCS_MULTILINE and
        // still leave Win32 laying nine narrow labels out in one row, and the
        // 35974677491 run measured exactly that (`page top 114 strip 14/14 rows 2`
        // — the one-row state, reported as a two-row plan). What every cycle
        // asserts is the transition itself, and this is the value it starts from.
        const int baseShift = base.app.stripShift[tab];

        for (int cycle = 1; cycle <= 3; ++cycle) {
            // --- the labels grow: the strip wraps, the page top moves DOWN ----
            KieeKeyProbeFontScale(dlg, kWrapFontPct);
            KieeKeyProbeReflowNow(dlg);
            KieeKeyProbeSetOffset(dlg, 0);
            HarnessState wrapped;
            readHarnessState(dlg, all, tab, &wrapped);
            ++g_fuzzChecks;
            // The proof: the strip's height grew and the page paid for it — the
            // rows the labels now need are the ones the content was pushed down by.
            // Both numbers are the app's own (`stripShift` is derived from the tab
            // control's display rectangle, `stripRows` from the plan), and the page
            // top may not move UP while the labels grow.
            const int wrapGrow = wrapped.app.stripShift[tab] - baseShift;
            if (measurable) {
                harnessAssert(dlg, wrapped, "strip_cycle_wrapped", cycle, 0,
                              kWrapFontPct, findings);
            }
            if (measurable && (wrapGrow <= 0 || wrapped.page.top < basePageTop)) {
                // A cycle that cannot reach the transition may not report it as
                // corrected: the state below is one where the strip is still one
                // row (or nothing was pushed down), so there is nothing to see.
                harnessFail(1, findings,
                            "the nine tab labels did not wrap at font " +
                                std::to_string(kWrapFontPct) + "% (strip shift " +
                                std::to_string(baseShift) + " -> " +
                                std::to_string(wrapped.app.stripShift[tab]) +
                                " px, rows " +
                                std::to_string(wrapped.app.stripRows) + ", page top " +
                                std::to_string(wrapped.page.top) + " vs the one-row " +
                                std::to_string(basePageTop) + ", stripShift seen " +
                                std::to_string(wrapped.app.stripSeen[tab]) +
                                " px) — the grow/shrink transition cannot be measured "
                                "in this state, so a green here would be a green that "
                                "never ran",
                            whereOf(wrapped, tab, passDpi, cycle, 0, stripW, clientH,
                                    kWrapFontPct));
            }

            // --- the labels shrink back: the strip fits one row again ---------
            KieeKeyProbeFontScale(dlg, 100);
            KieeKeyProbeReflowNow(dlg);
            KieeKeyProbeSetOffset(dlg, 0);
            HarnessState back;
            readHarnessState(dlg, all, tab, &back);
            harnessAssert(dlg, back, "strip_cycle_back", cycle, 0, 100, findings);
            int backFirstY = -1;
            int backFirstId = 0;
            for (const HarnessCtl& c : back.ctls) {
                if (!c.shown || c.regionEmpty) { continue; }
                if (backFirstY < 0 || c.y < backFirstY) { backFirstY = c.y; backFirstId = c.id; }
            }
            const std::string where =
                whereOf(back, tab, passDpi, cycle, 0, stripW, clientH, 100);

            // (a) the page top comes back — and the app's own plan says the strip
            //     is one row again, which is what "back" means
            ++g_fuzzChecks;
            if (back.page.top != basePageTop || back.app.stripShift[tab] != baseShift) {
                harnessFail(1, findings,
                            "the page top did not return to its one-row value: " +
                                std::to_string(basePageTop) + " -> " +
                                std::to_string(wrapped.page.top) + " (wrapped) -> " +
                                std::to_string(back.page.top) + " with strip shift " +
                                std::to_string(back.app.stripShift[tab]) +
                                " px (started this cycle at " +
                                std::to_string(baseShift) +
                                ", wrapped at " +
                                std::to_string(wrapped.app.stripShift[tab]) +
                                ", " + std::to_string(wrapped.app.stripRows) +
                                " rows) — the display rectangle of the wrapped strip "
                                "is still in force",
                            where + " " + harnessStateStr(back, "strip_cycle", cycle, 0, 100));
            }
            // (b) the tab-strip shift comes back to the value it had while the
            //     strip was one row. NOT "back to zero": since BS-22 the solver's
            //     input is the AUTHORED geometry, so the shift it records is the
            //     absolute distance between the authored top and the display
            //     rectangle — and the design (BS-10) is that the content starts at
            //     the display rectangle, not above it. For this dialog the tab
            //     frame's one-row display top is 14 px (96 dpi) BELOW the authored
            //     first row (group boxes at y=100, display rect top 114), so 14 px
            //     is the correct one-row state and `!= 0` called it a defect.
            //     What must never happen is a value that GROWS and stays grown
            //     (BS-21/BS-22: 14 -> 44 -> 74 ... with the content parked under
            //     the page). Comparing with the state this cycle started from
            //     catches exactly that, in both directions, and the live geometry
            //     is asserted separately by (a) and (c) — which is what makes this
            //     comparison legitimate rather than a loosened bound.
            ++g_fuzzChecks;
            if (back.app.stripShift[tab] != baseShift && back.page.top == basePageTop) {
                harnessFail(1, findings,
                            "the tab-strip shift did not come back to its one-row "
                            "value after the strip fitted one row again: stripShift[" +
                                std::to_string(tab) + "] = " +
                                std::to_string(base.app.stripShift[tab]) + " -> " +
                                std::to_string(back.app.stripShift[tab]) +
                                " px (96 dpi) — the wrap is baked into the layout, "
                                "so the content sits that far below the page it "
                                "belongs to",
                            where + " " + harnessStateStr(back, "strip_cycle", cycle, 0, 100));
            }
            // (c) the content returns to where it was (no accumulated drift)
            ++g_fuzzChecks;
            if (backFirstY < 0 || std::abs(backFirstY - baseFirstY) > 1) {
                harnessFail(1, findings,
                            "the page content did not return to its one-row position: "
                            "id " + std::to_string(backFirstId > 0 ? baseFirstId : 0) +
                            " was at y=" +
                                std::to_string(baseFirstY) + ", is at y=" +
                                std::to_string(backFirstY) + " after the strip fitted one "
                                "row again (moved by " +
                                std::to_string(backFirstY - baseFirstY) + " px)",
                            where + " " + harnessStateStr(back, "strip_cycle", cycle, 0, 100));
            }
            // (d) the ordinary invariants hold in the restored state too
            ++g_fuzzChecks;
            harnessAssert(dlg, back, "strip_cycle", cycle, 0, 100, findings);
        }
    }

    // Leave the dialog as this scenario found it.
    KieeKeyProbeResize(dlg, wideW, clientH);
    KieeKeyProbeReflowNow(dlg);
    KieeKeyProbeSetOffset(dlg, 0);
    KieeKeyProbeSelectTab(dlg, 0);
    return g_fuzzFailures - before;
}

int runSequenceHarness(HWND dlg, int tabCount, unsigned nativeDpi, unsigned passDpi,
                       int stepsPerTab,
                       std::vector<std::pair<std::string, int>>* byKind) {
    std::vector<HWND> all = stableChildren(dlg);
    RECT origClient{};
    origClient.right = 800;
    origClient.bottom = 600;
    ::GetClientRect(dlg, &origClient);
    std::vector<Finding> findings;
    const int before = g_fuzzFailures;

    const int scenarioFailures =
        harnessScenario(dlg, all, tabCount, nativeDpi, origClient, &findings);
    // v1.3.0-beta8fix1 (bug BS-21): the tab-strip reshape transition, driven
    // deterministically (one row -> multi-row -> one row, three cycles, every
    // tab) instead of waiting for the seeded walk to stumble into it.
    harnessScenarioStripCycles(dlg, all, tabCount, passDpi, nativeDpi,
                            origClient, &findings);

    static const int kFontScales[] = {100, 125, 150};
    for (int fontPct : kFontScales) {
        KieeKeyProbeFontScale(dlg, fontPct);
        for (int seed = 1; seed <= 8; ++seed) {
            HarnessRng rng(static_cast<unsigned>(seed) * 2654435761U + 7U);
            int curTab = rng.pick(0, tabCount - 1);
            KieeKeyProbeSelectTab(dlg, curTab);
            int font = fontPct;
            for (int step = 0; step < stepsPerTab; ++step) {
                const int op = rng.pick(0, static_cast<int>(kOpCount) - 1);
                harnessStep(dlg, all, tabCount, &curTab, &font, nativeDpi, origClient,
                            rng, op, step, seed, &findings);
                ++g_fuzzSteps;
                all = stableChildren(dlg);   // a solve can create/destroy children
            }
        }
    }
    // v1.3.0-beta8fix1 (bug BS-19 / R1): the fuzz ends on a RANDOM dpi
    // (96/120/144/192) and the cleanup below restores everything that has a
    // restore path — the font scale, the client SIZE, the offset, the tab — but a
    // size is not a scale: KieeKeyProbeResize() sets the client rectangle, it does
    // not change the app's dpi belief. So the dpi is restored explicitly, through
    // the same app path the fuzz used, and the R1 assertion that follows is then a
    // contract that can pass instead of a known failure (CI run 35875728560: R1
    // 6/6, "handed over a dialog solved at dpi 192 but this pass audits dpi 96").
    KieeKeyProbeSimulateDpi(dlg, passDpi);
    KieeKeyProbeFontScale(dlg, 100);
    KieeKeyProbeResize(dlg, static_cast<int>(origClient.right),
                       static_cast<int>(origClient.bottom));
    KieeKeyProbeReflowNow(dlg);
    KieeKeyProbeSetOffset(dlg, 0);
    KieeKeyProbeSelectTab(dlg, 0);

    // ---- R1 — THE HANDOVER (v1.3.0-beta8fix1, bug BS-19) --------------------
    //
    // This harness runs BETWEEN the scale passes (main() calls it at the end of
    // each pass), and the fuzz drives the dialog through 96/120/144/192 dpi,
    // text scales 100/125/150 % and window sizes 70..130 % of the pass's client.
    // Whatever is left here IS the dialog the next scale pass audits, so the
    // handover is part of the measurement and is asserted like one.
    //
    // It was not, and CI run 35866022217 shows what that costs: the fuzz's last
    // DPI op could leave the app at 192 dpi while the cleanup restored the
    // 96-dpi CLIENT SIZE (KieeKeyProbeResize sets a size, it does not change a
    // scale) — a dialog whose content was laid out at 2x inside a window sized
    // for 1x. The 150 % pass then rescaled that by 144/192 = 0.75 and audited
    // the result: children 720 px wide (480 x 2 x 0.75 — exactly 1.5 x the
    // AUTHORED 480) in a 399 px client, i.e. 100 `outside_page`, 513 I12 and 7
    // `clip` findings that describe the harness's leftover, not the app's 150 %
    // layout — with the audit's red light pointing at the product.
    // The pass that follows must get the state its own DPI declares: same DPI,
    // same client size, font scale 100, offset 0, tab 0, solved.
    {
        HarnessState handed;
        readHarnessState(dlg, all, 0, &handed);
        const std::string handover =
            "pass dpi=" + std::to_string(passDpi) + " handed over: app dpi=" +
            std::to_string(handed.app.dpi) + " client " +
            std::to_string(handed.client.right) + "x" +
            std::to_string(handed.client.bottom) + " (pass started with " +
            std::to_string(origClient.right) + "x" +
            std::to_string(origClient.bottom) + ") page " +
            rectStr(handed.page) + " offset " + std::to_string(handed.app.offset) +
            " range " + std::to_string(handed.app.range) +
            " baseline " + (handed.app.haveBaseline != 0 ? "yes" : "NO") +
            " visible " + std::to_string(handed.visibleCount);
        g_handoverNotes.push_back(handover);
        std::printf("ui_probe: [handover] %s\n", handover.c_str());

        ++g_invChecks[13];
        if (handed.app.dpi != passDpi) {
            harnessFail(13, &findings,
                        "the harness handed the next step a dialog solved at dpi " +
                            std::to_string(handed.app.dpi) + " but this pass audits dpi " +
                            std::to_string(passDpi) +
                            " — the cleanup restores the client SIZE, the font scale, the "
                            "offset and the tab, but not the DPI, so every finding measured "
                            "after this point describes the fuzz's leftover layout",
                        harnessStateStr(handed, "restore", stepsPerTab, 0, 100));
        }
        ++g_invChecks[13];
        // The client WIDTH is a function of WS_VSCROLL (a scrollbar is non-client):
        // a handover whose width differs from the pass's own by exactly one
        // scrollbar is the same window with the bar on the other side of its
        // decision, not a leftover size. The HEIGHT stays exact — the bar cannot
        // change it, so a height difference is a resize that was not undone.
        const int barW = ::GetSystemMetrics(SM_CXVSCROLL);
        const int widthDelta = static_cast<int>(handed.client.right - origClient.right);
        const bool widthOk = std::abs(widthDelta) <= 2 ||
                             (barW > 0 && std::abs(std::abs(widthDelta) - barW) <= 1);
        if (!widthOk || std::abs(handed.client.bottom - origClient.bottom) > 2) {
            harnessFail(13, &findings,
                        "the harness handed the next step a dialog of " +
                            std::to_string(handed.client.right) + "x" +
                            std::to_string(handed.client.bottom) +
                            " but this pass started at " +
                            std::to_string(origClient.right) + "x" +
                            std::to_string(origClient.bottom) +
                            " (a difference of one scrollbar width, " +
                            std::to_string(barW) + " px, is accepted: the client width "
                            "is a function of WS_VSCROLL) — the pass would audit a "
                            "window size no operation of this pass ever set",
                        harnessStateStr(handed, "restore", stepsPerTab, 0, 100));
        }
        // The restored state is judged by the same I1..I12 battery every
        // operation is: a handover that is scaled right but incoherent is not a
        // handover.
        harnessAssert(dlg, handed, "restore", stepsPerTab, 0, 100, &findings);
    }

    const int failures = g_fuzzFailures - before;
    for (const Finding& f : findings) { harnessKind(byKind, f.kind); }
    harnessTrace("{\"harness\": \"summary\", \"steps\": " + std::to_string(g_fuzzSteps) +
                 ", \"checks\": " + std::to_string(g_fuzzChecks) +
                 ", \"violations\": " + std::to_string(failures) +
                 ", \"scenarioFailures\": " + std::to_string(scenarioFailures) +
                 ", \"seeds\": 8, \"fontScales\": \"100/125/150\", \"stepsPerTab\": " +
                 std::to_string(stepsPerTab) + "}");
    for (std::size_t i = 1; i <= 12; ++i) {
        if (g_invChecks[i] > 0) {
            std::printf("  [inv] I%-2zu %6d checks, %d violations\n", i,
                        g_invChecks[i], g_invFailures[i]);
        }
    }
    for (const Finding& f : findings) {
        std::printf("    [%s] %s\n", f.kind.c_str(), f.detail.c_str());
    }
    g_findings += static_cast<int>(findings.size());
    return failures;
}

} // namespace

int main(int argc, char** argv) {
    std::string outDir = ".";
    if (argc > 1 && argv[1] != nullptr && argv[1][0] != '\0') { outDir = argv[1]; }
    // Steps per tab per (DPI, text scale) for the state-sequence harness. The
    // count is FIXED (not time-based) so two runs of one build are identical.
    int fuzzSteps = 60;
    if (argc > 2 && argv[2] != nullptr) {
        const int parsed = std::atoi(argv[2]);
        if (parsed > 0 && parsed <= 4000) { fuzzSteps = parsed; }
    }
    (void)::SetConsoleOutputCP(CP_UTF8);

    // Per-monitor v2 so the app scales exactly as it does for a real user.
    (void)::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX ice{};
    ice.dwSize = sizeof(ice);
    ice.dwICC = ICC_TAB_CLASSES | ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
    ::InitCommonControlsEx(&ice);

    KieeKeyProbeInit(::GetModuleHandleW(nullptr));
    {
        const unsigned appSize = KieeKeyProbeScrollStateSize();
        const unsigned probeSize = static_cast<unsigned>(sizeof(ProbeScrollStateT));
        if (appSize != probeSize) {
            std::printf("ui_probe: FAIL — the app reports a scroll state of %u bytes, "
                        "this probe expects %u: the mirror structs drifted (a field "
                        "added on one side only). Refusing to run — reading a "
                        "mismatched struct is a crash, not a measurement.\n",
                        appSize, probeSize);
            return 6;
        }
        std::printf("ui_probe: scroll state %u bytes (app == probe)\n", appSize);
    }
    HWND dlg = KieeKeyProbeOpenSettings(0);
    if (dlg == nullptr) {
        std::printf("ui_probe: FAIL — the settings dialog could not be created\n");
        return 3;
    }
    const UINT nativeDpi = ::GetDpiForWindow(dlg);
    std::printf("ui_probe: settings dialog %p, dpi=%u (%d%%)\n",
                static_cast<void*>(dlg), static_cast<unsigned>(nativeDpi),
                static_cast<int>((nativeDpi * 100U) / 96U));

    std::vector<std::pair<std::string, int>> byKind;
    const auto noteKind = [&byKind](const std::string& kind) {
        for (auto& entry : byKind) {
            if (entry.first == kind) { ++entry.second; return; }
        }
        byKind.emplace_back(kind, 1);
    };

    const HWND tabsCtl = ::GetDlgItem(dlg, IDC_TAB);
    LRESULT tabCount = tabsCtl != nullptr ? ::SendMessageW(tabsCtl, TCM_GETITEMCOUNT, 0, 0) : 0;
    if (tabCount <= 0) { tabCount = 9; }

    // The scale pass list: the runner's own DPI first, then the 150 % layout
    // through the app's real DPI-change path (the scale most users run).
    struct ScalePass { int percent; UINT dpi; };
    std::vector<ScalePass> passes;
    passes.push_back({static_cast<int>((nativeDpi * 100U) / 96U), nativeDpi});
    // v1.3.0-beta8fix1 (bug BS-19): the three scales the regression contract
    // names — 100 %, 125 % and 150 % — are each audited as their own pass,
    // with their own client size and their own operation-sequence harness, so
    // a leak between passes is measured instead of assumed away.
    if (nativeDpi != 120U) { passes.push_back({125, 120U}); }
    if (nativeDpi != 144U) { passes.push_back({150, 144U}); }

    std::string json;
    json += "{\n \"tool\": \"kieekey_ui_probe\",\n";
    json += " \"nativeDpi\": " + std::to_string(static_cast<unsigned>(nativeDpi)) + ",\n";
    json += " \"tabs\": [\n";
    int totalControls = 0;
    bool firstTabEntry = true;

    for (const ScalePass& pass : passes) {
        // The size this pass considers its own: whatever the window is at the
        // moment the native pass starts, re-scaled for the other passes — the
        // same relation KieeKeyProbeSimulateDpi() applies to the WINDOW when it
        // changes the scale, so a pass is reproducible on its own.
        RECT passClient{};
        ::GetClientRect(dlg, &passClient);
        // v1.3.0-beta8fix1 (bug BS-22c): A PASS DOES NOT INHERIT THE SCROLLBAR'S
        // WIDTH. The bar is the APP's decision, re-made for every geometry; a
        // client already deflated by it and then scaled hands the pass a window
        // one bar (plus one frame) narrower than the app's own S(560) client at
        // that DPI — a size no real session produces, and the source of the 125 %
        // pass's `clip`/`outside_page` findings. Measured arithmetic for the
        // 125 % pass of the 76f955a run: 96-dpi client 526 (the bar had taken 17
        // off the authored 543) × 1.25 = 658, minus the 120-dpi bar 21 = 637,
        // while the authored content needs 528 + S(12) = 540 @96 = 675 @125 and
        // the app's own client at 125 % is 700 (S(560)) - 21 = 679. The bar was
        // charged twice; the 42 px it cost are the whole finding class.
        if ((::GetWindowLongPtrW(dlg, GWL_STYLE) & WS_VSCROLL) != 0) {
            const int barW = ::GetSystemMetrics(SM_CXVSCROLL);
            passClient.right += (barW > 0 ? barW : 0);
        }
        if (pass.dpi != nativeDpi) {
            passClient.right = static_cast<LONG>(
                ::MulDiv(passClient.right, static_cast<int>(pass.dpi),
                         static_cast<int>(nativeDpi)));
            passClient.bottom = static_cast<LONG>(
                ::MulDiv(passClient.bottom, static_cast<int>(pass.dpi),
                         static_cast<int>(nativeDpi)));
        }
        // v1.3.0-beta8fix1 (bug BS-19): EVERY PASS ESTABLISHES ITS OWN STATE.
        //
        // A pass used to inherit whatever the previous pass's harness left behind
        // (see the R1 handover contract at the end of runSequenceHarness). The
        // handover is now asserted, but an assertion is not a remedy: the audit
        // itself must not depend on it. So the pass states its own preconditions
        // the same way the operation-sequence harness does after its cleanup —
        // app dpi == this pass's dpi, font scale 100, client size == the pass's
        // client size, offset 0, tab 0, and a solve of the app's own — and only
        // then starts measuring. KieeKeyProbeResize() sizes the window; it does
        // NOT change the app's DPI belief, which is exactly the asymmetry the
        // leftover state lived in.
        if (KieeKeyProbeSimulateDpi(dlg, pass.dpi) <= 0) {
            std::printf("ui_probe: FAIL — the pass dpi %u could not be established\n",
                        static_cast<unsigned>(pass.dpi));
            return 5;
        }
        KieeKeyProbeFontScale(dlg, 100);
        KieeKeyProbeResize(dlg, static_cast<int>(passClient.right),
                           static_cast<int>(passClient.bottom));
        KieeKeyProbeReflowNow(dlg);
        KieeKeyProbeSetOffset(dlg, 0);
        KieeKeyProbeSelectTab(dlg, 0);
        {
            HarnessState ready;
            readHarnessState(dlg, stableChildren(dlg), 0, &ready);
            const std::string entryNote =
                "pass @%d%% start: app dpi " + std::to_string(ready.app.dpi) +
                " client " + std::to_string(ready.client.right) + "x" +
                std::to_string(ready.client.bottom) + " page " + rectStr(ready.page) +
                " offset " + std::to_string(ready.app.offset) + " baseline " +
                (ready.app.haveBaseline != 0 ? "yes" : "NO");
            g_passEntryNotes.push_back(entryNote);
            std::printf("ui_probe: pass @%d%% starts: app dpi %u client %ldx%ld page %s "
                        "offset %d baseline %s\n",
                        pass.percent, ready.app.dpi,
                        static_cast<long>(ready.client.right),
                        static_cast<long>(ready.client.bottom),
                        rectStr(ready.page).c_str(),
                        ready.app.offset, ready.app.haveBaseline != 0 ? "yes" : "NO");
        }

        for (int tab = 0; tab < static_cast<int>(tabCount); ++tab) {
            if (KieeKeyProbeSelectTab(dlg, tab) != 0) { break; }
            ::Sleep(25);

            // v1.3.0-beta8 (probe): LET THE APP'S 500 ms TICK LAND, THEN STOP IT.
            //
            // Selecting a tab writes that tab's live text (uptime, arcade/AI
            // status, coach advice) and a row whose new text outgrows its box
            // asks for a reflow — which the app performs on its next 500 ms
            // tick, moving that row and everything below it. Measuring before
            // that tick reads a layout that is about to change: the 150 % pass
            // reported `clip` on rows whose live height was still the
            // pre-growth one (the finding's own message had the app's solver
            // agreeing with the probe on the needed height).
            //
            // So: one tick of grace per tab, then the timer stays off for the
            // audit — no check may race the app's repaint loop. The tick itself
            // is NOT skipped: `idle_jitter` further down sends WM_TIMER
            // directly (twice) and still proves an idle tick moves nothing.
            ::Sleep(700);
            ::KillTimer(dlg, 1);

            RECT client{};
            ::GetClientRect(dlg, &client);
            RECT page{0, 0, client.right, client.bottom};
            if (tabsCtl != nullptr) {
                RECT display{};
                ::GetClientRect(tabsCtl, &display);
                const RECT before = display;
                // TCM_ADJUSTRECT documents NO return value (it returns 0), so the
                // result is trusted and sanity-checked: anything outside the tab
                // control's own client rect would make the page checks vacuous.
                ::SendMessageW(tabsCtl, TCM_ADJUSTRECT, FALSE,
                               reinterpret_cast<LPARAM>(&display));
                const bool sane = display.right > display.left && display.bottom > display.top &&
                                  display.left >= before.left && display.top >= before.top &&
                                  display.right <= before.right && display.bottom <= before.bottom;
                if (sane) {
                    POINT tl{display.left, display.top};
                    POINT br{display.right, display.bottom};
                    ::ClientToScreen(tabsCtl, &tl);
                    ::ClientToScreen(tabsCtl, &br);
                    ::ScreenToClient(dlg, &tl);
                    ::ScreenToClient(dlg, &br);
                    page = RECT{tl.x, tl.y, br.x, br.y};
                }
            }

            // Stable child list for this tab (geometry is re-read per check).
            std::vector<HWND> all;
            for (HWND c = ::GetWindow(dlg, GW_CHILD); c != nullptr;
                 c = ::GetWindow(c, GW_HWNDNEXT)) {
                if (c == tabsCtl) { continue; }
                if (::GetDlgCtrlID(c) == 0) { continue; }
                all.push_back(c);
            }

            // BS-16e: the settings dialog rewrites ~30 live rows twice a second.
            // Between the two screen reads that frames nothing but noise, so the
            // telemetry tick is stopped for the rest of the run (the geometry
            // checks drive the dialog themselves and never needed it).
            KieeKeyProbeFreezeUi(dlg);

            std::vector<Ctl> ctls;
            std::vector<Finding> findings;
            Audit a;
            a.dlg = dlg;
            a.tabsCtl = tabsCtl;
            a.client = client;
            a.page = page;
            a.tab = tab;
            a.scalePercent = pass.percent;
            a.findings = &findings;

            readCtls(dlg, all, tab, page, ctls);
            totalControls += static_cast<int>(ctls.size());

            // Scroll travel available to the user right now.
            SCROLLINFO siAll{};
            siAll.cbSize = sizeof(siAll);
            siAll.fMask = SIF_RANGE | SIF_PAGE;
            const bool haveScroll = ::GetScrollInfo(dlg, SB_VERT, &siAll) != FALSE;
            const int travelPx = haveScroll
                ? std::max(0, (siAll.nMax + 1) - static_cast<int>(siAll.nPage))
                : 0;

            // ---- v1.3.0-beta8 (bug BS-13): the dialog must CLIP its children --
            // DialogBox() gives every dialog WS_CLIPCHILDREN; a hand-rolled
            // CreateWindowEx window does not, and this dialog carries ~124
            // controls plus a background brush — so the parent erased over
            // them on every repaint (flicker while idle, stale text left
            // behind while scrolling: "chữ bị duplicated").
            if (tab == 0) {
                ++g_checks;
                if ((::GetWindowLongPtrW(dlg, GWL_STYLE) & WS_CLIPCHILDREN) == 0) {
                    findings.push_back({"no_clipchildren",
                        "the settings window has no WS_CLIPCHILDREN: its background is painted "
                        "over its own child controls on every repaint"});
                }
            }

            // ---- v1.3.0-beta8 (bug UX-01): the wheel must never edit a value --
            // Win32 gives WM_MOUSEWHEEL to the control under the cursor, and a
            // closed drop-down list changes its SELECTION for it. The user's
            // charset combo went from Unicode to CP 1258 while they scrolled
            // the page with the wheel, and from then on every Vietnamese
            // keystroke was a raw byte ("gõ dấu thì bị ký tự lạ").
            int wheeledCombos = 0;
            for (const Ctl& c : ctls) {
                if (c.klass != "ComboBox" || c.tabpage != tab || !c.shown) { continue; }
                if (c.hwnd == nullptr) { continue; }
                const LRESULT before = ::SendMessageW(c.hwnd, CB_GETCURSEL, 0, 0);
                const LRESULT items  = ::SendMessageW(c.hwnd, CB_GETCOUNT, 0, 0);
                if (before < 0 || items <= 1) { continue; }
                ++g_checks;
                ++wheeledCombos;
                ::SendMessageW(c.hwnd, WM_MOUSEWHEEL,
                               MAKEWPARAM(0, static_cast<short>(-WHEEL_DELTA)), 0);
                const LRESULT after = ::SendMessageW(c.hwnd, CB_GETCURSEL, 0, 0);
                if (after != before) {
                    findings.push_back({"wheel_edit",
                        "id " + std::to_string(c.id) + " (ComboBox) changed its value " +
                        std::to_string(before) + " -> " + std::to_string(after) +
                        " from a MOUSE WHEEL — the wheel must scroll the dialog, "
                        "never edit a setting"});
                    ::SendMessageW(c.hwnd, CB_SETCURSEL, static_cast<WPARAM>(before), 0);
                }
            }

            // v1.3.0-beta8 (probe): PUT THE PAGE BACK AFTER THE WHEEL CHECK.
            //
            // Since UX-01 the app forwards a wheel over a combo to the DIALOG —
            // that is the whole fix: the wheel scrolls the page and never edits
            // a value. Which also means the geometry snapshot taken before that
            // loop is stale for every check below it, and the 150 % pass (the
            // scale with travel) reported exactly that: 12 phantom `hittest`
            // findings whose centres landed one row down, 2 `clip` findings on
            // rows that had scrolled out of the page, and `scroll_pos` offsets
            // that started life already scrolled. The wheel finding itself is
            // unaffected: the combo's selection did NOT move (that is the
            // contract being checked).
            if (wheeledCombos > 0) {
                ::SendMessageW(dlg, WM_VSCROLL, MAKEWPARAM(SB_TOP, 0), 0);
                ::Sleep(10);
                readCtls(dlg, all, tab, page, ctls);
            }

            checkSinglePage(a, ctls);
            checkRegions(a, ctls);
            checkPlanHeld(a, ctls);
            checkOverlaps(a, ctls);
            checkReach(a, ctls, travelPx);
            checkTextFit(a, ctls);
            checkHitTests(a, ctls);
            checkSiblingClobber(a, ctls);
            checkEmptyPage(a, ctls, travelPx);
            checkReflowWhileScrolled(a, dlg, all, page, ctls, travelPx);
            checkTabHeaders(a, static_cast<int>(tabCount), &findings);
            // Last, so the screenshot below records the cleaned frame.
            checkStalePixels(a);
            // ---- v1.3.0-beta8 (bug BS-13): the timer must not jitter --------
            // The 500 ms tick writes live text; a row that outgrows its box
            // asks the solver to re-solve ONCE. A second tick with nothing new
            // to say must therefore change no rectangle at all — the user's
            // "không kéo thì giật giật" was exactly this loop.
            {
                ::SendMessageW(dlg, WM_TIMER, 1, 0);
                ::Sleep(5);
                std::vector<Ctl> v0;
                readCtls(dlg, all, tab, page, v0);
                ::SendMessageW(dlg, WM_TIMER, 1, 0);
                ::Sleep(5);
                std::vector<Ctl> v1;
                readCtls(dlg, all, tab, page, v1);
                for (const Ctl& a0 : v0) {
                    for (const Ctl& a1 : v1) {
                        if (a0.id != a1.id || a0.klass != a1.klass) { continue; }
                        ++g_checks;
                        if (a0.x != a1.x || a0.y != a1.y || a0.w != a1.w || a0.h != a1.h) {
                            findings.push_back({"idle_jitter",
                                "id " + std::to_string(a1.id) + " (" + a1.klass + ") moved from " +
                                std::to_string(a0.x) + "," + std::to_string(a0.y) + " " +
                                std::to_string(a0.w) + "x" + std::to_string(a0.h) + " to " +
                                std::to_string(a1.x) + "," + std::to_string(a1.y) + " " +
                                std::to_string(a1.w) + "x" + std::to_string(a1.h) +
                                " between two idle timer ticks"});
                        }
                    }
                }
            }


            // ---- v1.3.0-beta8 (bug BS-12): the scroll path may MOVE, never ---
            //      RESIZE. Rows whose runtime text grows (the 500 ms timer's
            //      verdict / arcade / AI / coach lines ask for more height than
            //      the solver gave them) have to KEEP that height when the user
            //      scrolls: beta8 re-applied the SOLVED height on every step,
            //      so the line the user had just read was cut in half again —
            //      "khi kéo thì chữ loạn lên".
            //
            //      The app's timer would re-grow such a row mid-sweep, so the
            //      probe stops it first (it owns this dialog) and then does what
            //      the runtime growth path does: make ONE page control 20 px
            //      taller. The control is the deepest one that still fits in the
            //      reachable page, so its extra 20 px cannot overlap anything
            //      and cannot go out of reach — and its height is then measured
            //      at every scroll offset below.
            int grownId = 0;
            int grownH = 0;
            int grownW = 0;
            int grownOrigH = 0;
            HWND grownHwnd = nullptr;
            // The injected growth belongs to the PROBE, not to the app: every
            // geometry check below runs without it (it would otherwise be
            // reported as an overlap / out-of-page defect it just created).
            const auto dropInjected = [&](std::vector<Ctl>& v) {
                if (grownId == 0) { return; }
                v.erase(std::remove_if(v.begin(), v.end(),
                                       [&](const Ctl& c) { return c.id == grownId; }),
                        v.end());
            };
            if (travelPx > 0) {
                ::KillTimer(dlg, 1);   // the app's 500 ms text/layout timer
                ::Sleep(10);
                const Ctl* victim = nullptr;
                for (const Ctl& c : ctls) {
                    if (c.tabpage != tab || !c.shown || c.id == 0 || c.groupBox) { continue; }
                    if (c.hwnd == nullptr) { continue; }
                    if (victim == nullptr || c.y > victim->y) { victim = &c; }
                }
                if (victim != nullptr) {
                    ::SetWindowPos(victim->hwnd, nullptr, 0, 0, victim->w, victim->h + 20,
                                   SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                    grownId = victim->id;
                    grownH = victim->h + 20;
                    grownW = victim->w;
                    grownOrigH = victim->h;
                    grownHwnd = victim->hwnd;
                    readCtls(dlg, all, tab, page, ctls);
                }
            }

            // ---- the scroll path: the user's "khi kéo thì chữ loạn lên" -----
            //
            // Driven through real messages: SB_LINEDOWN/SB_PAGEDOWN/SB_BOTTOM are
            // exactly what the arrows, the wheel (which the app maps to line
            // steps) and the thumb's page-clicks send, and unlike a synthetic
            // SB_THUMBTRACK they do not depend on SCROLLINFO::nTrackPos being set
            // by a real drag — the first version of this audit sent SB_THUMBTRACK
            // and measured a scrollbar that never moved.
            if (travelPx > 0) {
                const int steps[] = {1, 2, 3, 4};
                int lastOffset = 0;
                // v1.3.0-beta8 (probe): the app's line step is DPI-SCALED
                // (S(16): 16 px at 96 dpi, 24 px at 150 %), so landing exactly
                // on the requested offset was never the contract — the tolerance
                // is one MEASURED notch, not a constant 16 px. What is a
                // contract: a notch always moves, never jumps a page, and the
                // offset ends at or past what was asked for.
                int stepMax = 16;
                for (const int step : steps) {
                    const int want = travelPx * step / 4;
                    int guard = 0;
                    while (guard++ < 64) {
                        SCROLLINFO si{};
                        si.cbSize = sizeof(si);
                        si.fMask = SIF_POS;
                        if (::GetScrollInfo(dlg, SB_VERT, &si) == FALSE) { break; }
                        const int at = static_cast<int>(si.nPos);
                        if (at >= want) { break; }
                        // One notch at a time: the same increment the wheel uses.
                        ::SendMessageW(dlg, WM_VSCROLL, MAKEWPARAM(SB_LINEDOWN, 0), 0);
                        SCROLLINFO sn{};
                        sn.cbSize = sizeof(sn);
                        sn.fMask = SIF_POS;
                        ++g_checks;
                        if (::GetScrollInfo(dlg, SB_VERT, &sn) != FALSE) {
                            const int moved = static_cast<int>(sn.nPos) - at;
                            if (moved > 0) { stepMax = std::max(stepMax, moved); }
                            if (moved <= 0) {
                                findings.push_back({"scroll_step",
                                    "SB_LINEDOWN moved nothing at offset " +
                                    std::to_string(at) + " — the wheel/arrow scroll is dead"});
                                break;
                            }
                            if (moved > 200) {
                                findings.push_back({"scroll_step",
                                    "SB_LINEDOWN jumped " + std::to_string(moved) +
                                    "px at offset " + std::to_string(at) +
                                    " — a line step must not skip a page"});
                            }
                        }
                    }
                    ::Sleep(5);
                    SCROLLINFO si{};
                    si.cbSize = sizeof(si);
                    si.fMask = SIF_POS;
                    ++g_checks;
                    const bool got = ::GetScrollInfo(dlg, SB_VERT, &si) != FALSE;
                    const int at = got ? static_cast<int>(si.nPos) : -1;
                    if (!got || at < want - stepMax || at > want + stepMax) {
                        findings.push_back({"scroll_pos",
                            "asked for offset " + std::to_string(want) +
                            " (line steps), the app reports " + std::to_string(at) +
                            " of a " + std::to_string(travelPx) + " px travel"});
                    }
                    if (at == lastOffset) { break; }   // no progress: stop probing
                    lastOffset = at;
                    readCtls(dlg, all, tab, page, ctls);
                    if (grownId != 0) {   // BS-12: the runtime height must survive
                        for (const Ctl& c : ctls) {
                            if (c.id != grownId) { continue; }
                            ++g_checks;
                            if (c.h < grownH) {
                                findings.push_back({"scroll_resize",
                                    "id " + std::to_string(c.id) + " (" + c.klass + ") was " +
                                    std::to_string(grownH) + "px tall before scrolling and is " +
                                    std::to_string(c.h) + "px at offset " + std::to_string(at) +
                                    " of " + std::to_string(travelPx) +
                                    " — the scroll path RESIZED a control (the app owns the "
                                    "rects; runtime growth must survive)"});
                            }
                        }
                        dropInjected(ctls);
                    }
                    Audit sa = a;
                    sa.prefix = "at scroll " + std::to_string(at) + "/" +
                                std::to_string(travelPx) + ": ";
                    checkRegions(sa, ctls);
                    checkOverlaps(sa, ctls);
                    checkHitTests(sa, ctls);
                }
                // The end of the travel must bring the deepest row into view.
                ::SendMessageW(dlg, WM_VSCROLL, MAKEWPARAM(SB_BOTTOM, 0), 0);
                ::Sleep(5);
                readCtls(dlg, all, tab, page, ctls);
                {
                    dropInjected(ctls);
                    Audit sa = a;
                    sa.prefix = "at the bottom of the travel: ";
                    int deepestTop = a.page.top;
                    for (const Ctl& c : ctls) {
                        if (c.tabpage == tab && c.shown) {
                            deepestTop = std::max(deepestTop, c.y);
                        }
                    }
                    for (const Ctl& c : ctls) {
                        if (c.tabpage != tab || !c.shown) { continue; }
                        ++g_checks;
                        if (c.y + c.h > a.page.bottom + 1 && c.y <= deepestTop + 1) {
                            findings.push_back({"scroll_reach",
                                "at the bottom of the travel (" + std::to_string(travelPx) +
                                " px) id " + std::to_string(c.id) + " (" + c.klass + ") at " +
                                rectStr(c.x, c.y, c.w, c.h) + " is still below the page " +
                                rectStr(a.page)});
                        }
                    }
                    checkOverlaps(sa, ctls);
                    checkRegions(sa, ctls);
                }
                // Back to the top: the next tab (and the screenshot) expect it.
                ::SendMessageW(dlg, WM_VSCROLL, MAKEWPARAM(SB_TOP, 0), 0);
                ::Sleep(5);
                readCtls(dlg, all, tab, page, ctls);
                // Undo the probe's own growth: the next tab (and the audit
                // snapshot that follows) must see the app's layout, not ours.
                if (grownHwnd != nullptr) {
                    ::SetWindowPos(grownHwnd, nullptr, 0, 0, grownW, grownOrigH,
                                   SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                    grownId = 0;
                }
            }

            for (const Finding& f : findings) { noteKind(f.kind); }
            g_findings += static_cast<int>(findings.size());

            const std::wstring shot = toWide(outDir) + L"\\tab" + std::to_wstring(tab) +
                                      (pass.percent == 100 ? L"" :
                                       L"_" + std::to_wstring(pass.percent)) + L".png";
            int capW = 0;
            int capH = 0;
            const bool shotOk = captureWindow(dlg, shot, &capW, &capH);
            const std::string shotName = toUtf8(shot.substr(toWide(outDir).size() + 1));

            if (!firstTabEntry) { json += ",\n"; }
            firstTabEntry = false;
            json += "  {\"tab\": " + std::to_string(tab) + ", \"scale\": " +
                    std::to_string(pass.percent) + ", \"controls\": " +
                    std::to_string(ctls.size()) + ", \"dialog\": [" +
                    std::to_string(client.right) + "," + std::to_string(client.bottom) +
                    "], \"page\": [" + std::to_string(page.left) + "," +
                    std::to_string(page.top) + "," + std::to_string(page.right) + "," +
                    std::to_string(page.bottom) + "], \"scrollTravel\": " +
                    std::to_string(travelPx) + ", \"screenshot\": \"" +
                    (shotOk ? jsonEscape(shotName) : std::string()) + "\", \"findings\": [";
            for (std::size_t i = 0; i < findings.size(); ++i) {
                json += std::string(i > 0 ? ", " : "") + std::string("{\"kind\": \"") +
                        findings[i].kind + "\", \"detail\": \"" +
                        jsonEscape(findings[i].detail) + "\"}";
            }
            // Compact geometry digest: what is where, so the layout can be
            // reconstructed (and diffed against a screenshot) without the PNG.
            json += "], \"geometry\": [";
            for (std::size_t i = 0; i < ctls.size(); ++i) {
                const Ctl& c = ctls[i];
                if (!c.shown) { continue; }
                json += std::string(i > 0 && !json.empty() ? ", " : "") ;
                json += "\"" + std::to_string(c.id) + ": " +
                        rectStr(c.x, c.y, c.w, c.h) +
                        (c.hasRegion ? (" r" + rectStr(c.region)) : std::string()) +
                        (c.tabpage < 0 ? " chrome" : "") + "\"";
            }
            json += "]}";

            std::printf("  tab %d @%d%%: %d controls, %d findings%s (page %d,%d..%d,%d "
                        "dialog %dx%d travel %d)\n",
                        tab, pass.percent, static_cast<int>(ctls.size()),
                        static_cast<int>(findings.size()),
                        shotOk ? "" : " (screenshot failed)", static_cast<int>(page.left),
                        static_cast<int>(page.top), static_cast<int>(page.right),
                        static_cast<int>(page.bottom), static_cast<int>(client.right),
                        static_cast<int>(client.bottom), travelPx);
            for (const Finding& f : findings) {
                std::printf("    [%s] %s\n", f.kind.c_str(), f.detail.c_str());
            }
        }

        // v1.3.0-beta8fix1: the state-sequence half — the named display-change
        // scenario plus the seeded fuzz pass, for THIS scale pass.
        runSequenceHarness(dlg, static_cast<int>(tabCount), nativeDpi, pass.dpi,
                           fuzzSteps, &byKind);
    }

    // The harness's own counters (v1.3.0-beta8fix1). They exist so a green run
    // cannot mean "the operation-sequence checks never ran": the step count, the
    // number of evaluated invariant instances and the violations per invariant
    // are all in the report, and the same numbers ride in the CI digest line.
    std::string invSummary;
    for (std::size_t i = 1; i <= 13; ++i) {
        if (g_invChecks[i] == 0 && g_invFailures[i] == 0) { continue; }
        // Slot 13 is the handover contract ("R1"): it is not an app invariant
        // but it is measured, counted and reported the same way, because a
        // handover violation is what turns every finding after it into noise.
        invSummary += std::string(invSummary.empty() ? "" : " ") +
                      (i == 13 ? std::string("R1") : ("I" + std::to_string(i))) + ":" +
                      std::to_string(g_invChecks[i]) + "/" +
                      std::to_string(g_invFailures[i]);
    }

    json += "\n ],\n \"controls\": " + std::to_string(totalControls) +
            ",\n \"checks\": " + std::to_string(g_checks) +
            ",\n \"findings\": " + std::to_string(g_findings) +
            ",\n \"screenCaptures\": " + std::to_string(g_screenCaptures) +
            ",\n \"screenUnavailable\": " + std::to_string(g_screenUnavailable) +
            ",\n \"stripCycleUnavailable\": " +
                std::to_string(g_stripCycleUnavailable) +
            ",\n \"stripCycleNote\": \"" + g_stripCycleNote + "\"," +
            ",\n \"fuzzSteps\": " + std::to_string(g_fuzzSteps) +
            ",\n \"fuzzChecks\": " + std::to_string(g_fuzzChecks) +
            ",\n \"fuzzViolations\": " + std::to_string(g_fuzzFailures) +
            ",\n \"scenarios\": " + std::to_string(g_scenarioRuns) +
            ",\n \"scenarioViolations\": " + std::to_string(g_scenarioFailures) +
            ",\n \"invariants\": \"" + jsonEscape(invSummary) + "\"" +
            ",\n \"traceLines\": " + std::to_string(g_traceLines) +
            ",\n \"passEntries\": [";
    for (std::size_t i = 0; i < g_passEntryNotes.size() && i < 4; ++i) {
        json += std::string(i > 0 ? ", " : "") + "\"" + jsonEscape(g_passEntryNotes[i]) + "\"";
    }
    json += "],\n \"handover\": [";
    // v1.3.0-beta8fix1 (bug BS-19): the handover notes and one example state per
    // violated invariant ride in the JSON, so the CI digest can carry the state
    // that produced a count (see the workflow's operation-sequence line).
    for (std::size_t i = 0; i < g_handoverNotes.size() && i < 4; ++i) {
        json += std::string(i > 0 ? ", " : "") + "\"" + jsonEscape(g_handoverNotes[i]) + "\"";
    }
    json += "],\n \"firstViolations\": [";
    {
        bool firstEx = true;
        for (std::size_t i = 0; i < 14; ++i) {
            if (g_invExample[i].empty()) { continue; }
            json += std::string(firstEx ? "" : ", ") + "{\"inv\": \"" +
                    (i == 13 ? std::string("R1") : ("I" + std::to_string(i))) +
                    "\", \"state\": \"" + jsonEscape(g_invExample[i]) + "\"}";
            firstEx = false;
        }
    }
    json += "],\n \"findingsByKind\": {";
    for (std::size_t i = 0; i < byKind.size(); ++i) {
        json += std::string(i > 0 ? ", " : "") + "\"" + byKind[i].first + "\": " +
                std::to_string(byKind[i].second);
    }
    json += "}\n}\n";

    const std::wstring jsonPath = toWide(outDir) + L"\\ui_probe.json";
    if (!jsonBalanced(json)) {
        std::printf("ui_probe: FAIL - the report JSON does not balance (a missing '+' "
                    "between two adjacent literals?). NOT writing ui_probe.json: the "
                    "run must fail with this message instead of a parser exception.\n");
        return 4;
    }
    const bool jsonOk = writeAll(jsonPath, json.data(), json.size());
    // The operation-sequence trace: every invariant violation with the whole
    // state (offset, range, latch, style bit, viewport, page, depths, baseline)
    // plus a summary line, so a red run is diagnosable from the artefact alone.
    const std::wstring tracePath = toWide(outDir) + L"\\ui_probe_trace.jsonl";
    const bool traceOk = writeAll(tracePath, g_trace.data(), g_trace.size());
    std::printf("ui_probe: %d findings over %d controls / %d checks; json %s\n",
                g_findings, totalControls, g_checks, jsonOk ? "written" : "FAILED");
    std::printf("ui_probe: harness %d steps / %d invariant checks / %d violations "
                "(%d scenarios, %d with violations); trace %s (%d lines)\n",
                g_fuzzSteps, g_fuzzChecks, g_fuzzFailures, g_scenarioRuns,
                g_scenarioFailures, traceOk ? "written" : "FAILED", g_traceLines);
    if (!invSummary.empty()) {
        std::printf("ui_probe: invariants (checks/violations) %s\n", invSummary.c_str());
    }
    for (const auto& entry : byKind) {
        std::printf("  %-14s %d\n", entry.first.c_str(), entry.second);
    }
    return g_findings == 0 ? 0 : 1;
}
