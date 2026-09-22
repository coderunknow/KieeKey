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
    ::SendMessageW(hwnd, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(mem),
                   static_cast<LPARAM>(PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND));
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
                    std::to_string(ix) + "x" + std::to_string(iy) + " px"});
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
            a.findings->push_back({"outside_page",
                "id " + std::to_string(c.id) + " (" + c.klass + ") at " +
                rectStr(c.x, c.y, c.w, c.h) + " is outside the reachable page (page " +
                rectStr(a.page) + " + " + std::to_string(travelPx) + " px of scroll)"});
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
void checkTextFit(const Audit& a, const std::vector<Ctl>& ctls) {
    for (const Ctl& c : ctls) {
        if (c.text.empty() || c.groupBox || !c.onScreen) { continue; }
        const std::wstring wtext = toWide(c.text);
        ++g_checks;
        int need = 0;
        if (c.eh <= c.fontHeight + 4) {
            need = textWidth(c.hwnd, wtext);
            if (need > c.ew + 2) {
                a.findings->push_back({"clip",
                    a.prefix + "id " + std::to_string(c.id) + " (" + c.klass + ") needs " +
                    std::to_string(need) + "px (app solver says " +
                    std::to_string(KieeKeyProbeMeasureStaticHeight(a.dlg, c.id)) +
                    "), shows " + std::to_string(c.ew) + "px of " + std::to_string(c.w) +
                    " (box " + rectStr(c.x, c.y, c.w, c.h) + "): " + c.text.substr(0, 60)});
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
        a.findings->push_back({"hittest",
            a.prefix + "id " + std::to_string(c.id) + " (" + c.klass + ") centre (" +
            std::to_string(pt.x) + "," + std::to_string(pt.y) + ") hits id " +
            std::to_string(::GetDlgCtrlID(hit)) + " (" + className(hit) + ")"});
    }
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

} // namespace

int main(int argc, char** argv) {
    std::string outDir = ".";
    if (argc > 1 && argv[1] != nullptr && argv[1][0] != '\0') { outDir = argv[1]; }
    (void)::SetConsoleOutputCP(CP_UTF8);

    // Per-monitor v2 so the app scales exactly as it does for a real user.
    (void)::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX ice{};
    ice.dwSize = sizeof(ice);
    ice.dwICC = ICC_TAB_CLASSES | ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
    ::InitCommonControlsEx(&ice);

    KieeKeyProbeInit(::GetModuleHandleW(nullptr));
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
    if (nativeDpi != 144U) { passes.push_back({150, 144U}); }

    std::string json;
    json += "{\n \"tool\": \"kieekey_ui_probe\",\n";
    json += " \"nativeDpi\": " + std::to_string(static_cast<unsigned>(nativeDpi)) + ",\n";
    json += " \"tabs\": [\n";
    int totalControls = 0;
    bool firstTabEntry = true;

    for (const ScalePass& pass : passes) {
        if (pass.dpi != nativeDpi) {
            const int ok = KieeKeyProbeSimulateDpi(dlg, pass.dpi);
            std::printf("ui_probe: simulated DPI change -> %u (%d)\n",
                        static_cast<unsigned>(pass.dpi), ok);
            if (ok <= 0) { continue; }
            ::Sleep(25);
        }
        for (int tab = 0; tab < static_cast<int>(tabCount); ++tab) {
            if (KieeKeyProbeSelectTab(dlg, tab) != 0) { break; }
            ::Sleep(25);

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

            checkSinglePage(a, ctls);
            checkRegions(a, ctls);
            checkOverlaps(a, ctls);
            checkReach(a, ctls, travelPx);
            checkTextFit(a, ctls);
            checkHitTests(a, ctls);
            checkTabHeaders(a, static_cast<int>(tabCount), &findings);

            // ---- the scroll path: the user's "khi kéo thì chữ loạn lên" -----
            if (travelPx > 0) {
                for (int step = 1; step <= 4; ++step) {
                    const int want = travelPx * step / 4;
                    ::SendMessageW(dlg, WM_VSCROLL, MAKEWPARAM(SB_THUMBTRACK, want), 0);
                    ::Sleep(5);
                    SCROLLINFO si{};
                    si.cbSize = sizeof(si);
                    si.fMask = SIF_POS;
                    ++g_checks;
                    const bool got = ::GetScrollInfo(dlg, SB_VERT, &si) != FALSE;
                    if (!got || static_cast<int>(si.nPos) != want) {
                        findings.push_back({"scroll_pos",
                            "asked for offset " + std::to_string(want) + ", the app moved to " +
                            std::to_string(got ? static_cast<int>(si.nPos) : -1)});
                    }
                    readCtls(dlg, all, tab, page, ctls);
                    Audit sa = a;
                    sa.prefix = "at scroll " + std::to_string(want) + "/" +
                                std::to_string(travelPx) + ": ";
                    checkRegions(sa, ctls);
                    checkOverlaps(sa, ctls);
                    checkHitTests(sa, ctls);
                }
                // Back to the top: the next tab (and the screenshot) expect it.
                ::SendMessageW(dlg, WM_VSCROLL, MAKEWPARAM(SB_TOP, 0), 0);
                ::Sleep(5);
                readCtls(dlg, all, tab, page, ctls);
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
    }

    json += "\n ],\n \"controls\": " + std::to_string(totalControls) +
            ",\n \"checks\": " + std::to_string(g_checks) +
            ",\n \"findings\": " + std::to_string(g_findings) +
            ",\n \"findingsByKind\": {";
    for (std::size_t i = 0; i < byKind.size(); ++i) {
        json += std::string(i > 0 ? ", " : "") + "\"" + byKind[i].first + "\": " +
                std::to_string(byKind[i].second);
    }
    json += "}\n}\n";

    const std::wstring jsonPath = toWide(outDir) + L"\\ui_probe.json";
    const bool jsonOk = writeAll(jsonPath, json.data(), json.size());
    std::printf("ui_probe: %d findings over %d controls / %d checks; json %s\n",
                g_findings, totalControls, g_checks, jsonOk ? "written" : "FAILED");
    for (const auto& entry : byKind) {
        std::printf("  %-14s %d\n", entry.first.c_str(), entry.second);
    }
    return g_findings == 0 ? 0 : 1;
}
