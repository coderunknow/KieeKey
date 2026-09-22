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

namespace {

int g_checks = 0;
int g_findings = 0;

struct Finding {
    std::string kind;
    std::string detail;
};

struct Ctl {
    int         id = 0;
    std::string klass;
    std::string text;
    int         x = 0, y = 0, w = 0, h = 0;   // dialog client coordinates
    bool        groupBox = false;
    bool        interactive = false;
    int         fontHeight = 0;
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

} // namespace

int main(int argc, char** argv) {
    std::string outDir = ".";
    if (argc > 1 && argv[1] != nullptr && argv[1][0] != '\0') { outDir = argv[1]; }
    (void)::SetConsoleOutputCP(CP_UTF8);

    // Per-monitor v2 so the app scales exactly as it does for a real user (the
    // runner reports its own DPI; see the header for what stays MODELLED).
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
    const UINT dpi = ::GetDpiForWindow(dlg);
    std::printf("ui_probe: settings dialog %p, dpi=%u (%d%%)\n",
                static_cast<void*>(dlg), static_cast<unsigned>(dpi),
                static_cast<int>((dpi * 100U) / 96U));

    std::vector<std::pair<std::string, int>> byKind;
    const auto noteKind = [&byKind](const std::string& kind) {
        for (auto& entry : byKind) {
            if (entry.first == kind) { ++entry.second; return; }
        }
        byKind.emplace_back(kind, 1);
    };

    std::string json;
    json += "{\n \"tool\": \"kieekey_ui_probe\",\n";
    json += " \"dpi\": " + std::to_string(static_cast<unsigned>(dpi)) + ",\n";
    json += " \"scalePercent\": " + std::to_string(static_cast<int>((dpi * 100U) / 96U)) + ",\n";
    json += " \"note\": \"virtual 150% stays MODELLED (audit_layout.py CA-01a + manual M1)\",\n";
    json += " \"tabs\": [\n";

    const HWND tabsCtl = ::GetDlgItem(dlg, IDC_TAB);
    LRESULT tabCount = tabsCtl != nullptr ? ::SendMessageW(tabsCtl, TCM_GETITEMCOUNT, 0, 0) : 0;
    if (tabCount <= 0) { tabCount = 9; }

    int totalControls = 0;
    for (int tab = 0; tab < static_cast<int>(tabCount); ++tab) {
        if (KieeKeyProbeSelectTab(dlg, tab) != 0) { break; }
        ::Sleep(25);

        RECT client{};
        ::GetClientRect(dlg, &client);
        RECT page{0, 0, client.right, client.bottom};
        if (tabsCtl != nullptr) {
            // TCM_GETITEMRECT returns the tab HEADER button, not the page: the
            // display rectangle comes from TCM_ADJUSTRECT over the tab control's
            // own client rect (the first CI run used the header rect and reported
            // every control as "outside the page 99x112" — a probe bug, not a
            // product bug). The header rects are still used for the tab_header
            // check further down.
            RECT display{};
            ::GetClientRect(tabsCtl, &display);
            const RECT before = display;
            // TCM_ADJUSTRECT documents NO return value (it returns 0), so the
            // result must be trusted and sanity-checked — testing the return
            // value silently kept the fallback rectangle, i.e. the WHOLE dialog
            // client, which made every page check vacuous.
            (void)::SendMessageW(tabsCtl, TCM_ADJUSTRECT, FALSE,
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

        // The always-visible chrome lives OUTSIDE the tab page by design: the
        // header strip (icon/title/status), the in-app ON/OFF toggle and the
        // OK/Cancel/Apply row. `outside_page` must not count it — the second CI
        // run reported the three header statics as findings for exactly this
        // reason. Keep in sync with the always-visible controls in main.cpp.
        static const int kChromeIds[] = {IDOK, IDCANCEL, IDC_BTN_TOGGLE, IDC_BTN_APPLY,
                                         IDC_STAT_HEAD_ICON, IDC_STAT_HEAD_TITLE,
                                         IDC_STAT_HEAD_STATUS};
        const auto isChrome = [](int id) {
            for (const int chrome : kChromeIds) {
                if (chrome == id) { return true; }
            }
            return false;
        };

        std::vector<Ctl> ctls;
        std::vector<Finding> findings;

        // ---- collect ------------------------------------------------------
        for (HWND child = ::GetWindow(dlg, GW_CHILD); child != nullptr;
             child = ::GetWindow(child, GW_HWNDNEXT)) {
            if (child == tabsCtl || ::IsWindowVisible(child) == FALSE) { continue; }
            Ctl c;
            c.id = ::GetDlgCtrlID(child);
            c.klass = className(child);
            c.text = windowText(child);
            const RECT r = clientRectOf(dlg, child);
            c.x = r.left;
            c.y = r.top;
            c.w = static_cast<int>(r.right - r.left);
            c.h = static_cast<int>(r.bottom - r.top);
            const LONG_PTR style = ::GetWindowLongPtrW(child, GWL_STYLE);
            // BS_GROUPBOX is 0x7 (a triplet of flag bits), so `style & BS_GROUPBOX`
            // is ALSO true for BS_DEFPUSHBUTTON (0x1) — which silently excluded the
            // OK button from the overlap and hit-test checks (a false negative that
            // hid the fourth stacked button of BS-09).
            c.groupBox = c.klass == "Button" && (style & BS_TYPEMASK) == BS_GROUPBOX;
            c.interactive = c.klass == "Button" || c.klass == "Edit" || c.klass == "ComboBox" ||
                            c.klass == "ListBox";
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
            ctls.push_back(c);
        }
        totalControls += static_cast<int>(ctls.size());

        // ---- 1. inside the tab page ---------------------------------------
        // The page is a VIEWPORT: content below it is reachable through the
        // vertical scrollbar (BS-01), so the bottom bound is the reachable end
        // of the scroll range, not page.bottom. Anything deeper than that is
        // painted but unreachable — the beta7 complaint class.
        SCROLLINFO siReach{};
        siReach.cbSize = sizeof(siReach);
        siReach.fMask = SIF_RANGE | SIF_PAGE;
        const bool haveScroll = ::GetScrollInfo(dlg, SB_VERT, &siReach) != FALSE;
        const int travelPx = haveScroll
            ? std::max(0, (siReach.nMax + 1) - static_cast<int>(siReach.nPage))
            : 0;
        const int reachableBottom = page.bottom + travelPx;
        // Horizontal bound: the tab control's own CLIENT rectangle. The display
        // rectangle is narrower than the authored page whenever the control
        // reserves scrollbar space, and the app legitimately fills the authored
        // width — nothing is clipped or covered while a control stays inside the
        // tab control's client area. The fourth CI run flagged 17 group boxes for
        // exactly this 8 px difference.
        RECT tabClient{};
        if (tabsCtl != nullptr) {
            ::GetClientRect(tabsCtl, &tabClient);
            POINT tl{0, 0};
            POINT br{tabClient.right, tabClient.bottom};
            ::ClientToScreen(tabsCtl, &tl);
            ::ClientToScreen(tabsCtl, &br);
            ::ScreenToClient(dlg, &tl);
            ::ScreenToClient(dlg, &br);
            tabClient = RECT{tl.x, tl.y, br.x, br.y};
        } else {
            tabClient = RECT{0, 0, client.right, client.bottom};
        }
        for (const Ctl& c : ctls) {
            if (isChrome(c.id)) { continue; }
            ++g_checks;
            if (c.x + c.w > tabClient.right + 1 || c.y + c.h > reachableBottom + 1 ||
                c.x < tabClient.left - 1 || c.y < page.top - 1) {
                findings.push_back({"outside_page",
                    "id " + std::to_string(c.id) + " (" + c.klass + ") at " +
                    std::to_string(c.x) + "," + std::to_string(c.y) + " " +
                    std::to_string(c.w) + "x" + std::to_string(c.h) +
                    " is outside the reachable page (page " + std::to_string(page.left) +
                    "," + std::to_string(page.top) + ".." + std::to_string(tabClient.right) +
                    "," + std::to_string(reachableBottom) + ") x=" + std::to_string(c.x) +
                    (c.y + c.h > reachableBottom + 1 && travelPx > 0
                         ? " (deeper than the scroll range by " +
                               std::to_string(c.y + c.h - reachableBottom) + " px)"
                         : "")});
            }
        }

        // ---- 2. no overlaps ------------------------------------------------
        for (std::size_t i = 0; i < ctls.size(); ++i) {
            if (ctls[i].groupBox) { continue; }
            for (std::size_t j = i + 1; j < ctls.size(); ++j) {
                if (ctls[j].groupBox) { continue; }
                // A page control MAY sit under the floating chrome when the page
                // scrolls (that is what the fixed bottom row is for), so only
                // page-vs-page and chrome-vs-chrome pairs are compared.
                if (isChrome(ctls[i].id) != isChrome(ctls[j].id)) { continue; }
                ++g_checks;
                const int ix = std::min(ctls[i].x + ctls[i].w, ctls[j].x + ctls[j].w) -
                               std::max(ctls[i].x, ctls[j].x);
                const int iy = std::min(ctls[i].y + ctls[i].h, ctls[j].y + ctls[j].h) -
                               std::max(ctls[i].y, ctls[j].y);
                if (ix > kTouchTolerancePx && iy > kTouchTolerancePx) {
                    const auto at = [](const Ctl& c) {
                        return "at " + std::to_string(c.x) + "," + std::to_string(c.y) + " " +
                               std::to_string(c.w) + "x" + std::to_string(c.h);
                    };
                    findings.push_back({"overlap",
                        "id " + std::to_string(ctls[i].id) + " (" + ctls[i].klass + ") " +
                        at(ctls[i]) + " and id " + std::to_string(ctls[j].id) + " (" +
                        ctls[j].klass + ") " + at(ctls[j]) + " overlap by " +
                        std::to_string(ix) + "x" + std::to_string(iy) + " px"});
                }
            }
        }

        // ---- 3. real-font text fit -----------------------------------------
        for (const Ctl& c : ctls) {
            if (c.text.empty() || c.groupBox) { continue; }
            const HWND self = ::GetDlgItem(dlg, c.id);
            const std::wstring wtext = toWide(c.text);
            ++g_checks;
            if (c.h <= c.fontHeight + 4) {
                const int need = textWidth(self, wtext);
                if (need > c.w + 2) {
                    findings.push_back({"clip",
                        "id " + std::to_string(c.id) + " (" + c.klass + ") needs " +
                        std::to_string(need) + "px (app solver says " +
                        std::to_string(KieeKeyProbeMeasureStaticHeight(dlg, c.id)) +
                        "), has " + std::to_string(c.w) + "px (fontH " +
                        std::to_string(c.fontHeight) + "): " + c.text.substr(0, 60)});
                }
            } else {
                const int need = wrappedTextHeight(self, wtext, c.w);
                if (need > c.h + 2) {
                    findings.push_back({"clip",
                        "id " + std::to_string(c.id) + " (" + c.klass + ") wraps to " +
                        std::to_string(need) + "px (app solver says " +
                        std::to_string(KieeKeyProbeMeasureStaticHeight(dlg, c.id)) +
                        ") in a " + std::to_string(c.h) + "px box (w " + std::to_string(c.w) +
                        ", fontH " + std::to_string(c.fontHeight) + "): " +
                        c.text.substr(0, 60)});
                }
            }
        }

        // ---- 4. the scrollbar tells the truth (BS-01) ----------------------
        {
            ++g_checks;
            // PAGE content only: the floating chrome (bottom button row) sits
            // below the viewport BY DESIGN and is excluded from the depth the app
            // compares against the viewport, so including it here produced eight
            // "scrollbar disabled but content ends at y=677" findings whose 677 was
            // the button row's own bottom edge.
            int contentBottom = page.top;
            for (const Ctl& c : ctls) {
                if (isChrome(c.id)) { continue; }
                contentBottom = std::max(contentBottom, c.y + c.h);
            }
            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask = SIF_ALL;
            const bool have = ::GetScrollInfo(dlg, SB_VERT, &si) != FALSE;
            const bool enabled = have && si.nPage > 0 && si.nMax > static_cast<int>(si.nPage) - 1;
            const bool wanted = contentBottom > page.bottom + 1;
            if (enabled != wanted) {
                findings.push_back({"scrollbar",
                    std::string("scrollbar ") + (enabled ? "enabled" : "disabled") +
                    " but content ends at y=" + std::to_string(contentBottom) +
                    ", viewport ends at y=" + std::to_string(page.bottom) +
                    (wanted ? " — content is unreachable" : " — no overflow")});
            }
        }

        // ---- 5. hit-test: every interactive control owns its centre --------
        for (const Ctl& c : ctls) {
            if (!c.interactive || c.groupBox || c.w <= 0 || c.h <= 0) { continue; }
            ++g_checks;
            // c.x/c.y are dialog client coordinates already (clientRectOf), so
            // the point is absolute — the first CI run added page.left/top on
            // top of them, which sent every probe point to the wrong control.
            const POINT pt{c.x + c.w / 2, c.y + c.h / 2};
            // Only the part of the page that is on screen right now can be
            // clicked: a control scrolled below the viewport (BS-01 keeps them
            // there on purpose) has no reachable centre until the user scrolls.
            // Its reachability is the scrollbar check's business.
            if (pt.y >= client.bottom || pt.x >= client.right || pt.x < 0 || pt.y < 0) {
                continue;
            }
            POINT screenPt{pt.x, pt.y};
            ::ClientToScreen(dlg, &screenPt);
            // WindowFromPoint is the API the mouse input path itself uses (screen
            // coordinates; hidden and disabled windows are skipped). The second
            // CI run used RealChildWindowFromPoint, which answers inside the tab
            // control's own (larger) window rectangle and produced a batch of
            // "hits id 500 (SysTabControl32)" findings that no click could
            // reproduce. When the two APIs disagree the detail says so.
            const HWND hit = ::WindowFromPoint(screenPt);
            const HWND real = ::RealChildWindowFromPoint(dlg, pt);
            const HWND self = ::GetDlgItem(dlg, c.id);
            if (hit != nullptr && self != nullptr && hit != self && ::IsChild(self, hit) == FALSE) {
                std::string detail = "id " + std::to_string(c.id) + " centre (" +
                    std::to_string(pt.x) + "," + std::to_string(pt.y) + ") hits id " +
                    std::to_string(::GetDlgCtrlID(hit)) + " (" + className(hit) + ")";
                if (real != nullptr && real != hit) {
                    detail += "; RealChildWindowFromPoint says id " +
                              std::to_string(::GetDlgCtrlID(real));
                }
                findings.push_back({"hittest", detail});
            }
        }

        // ---- 6. tab headers -------------------------------------------------
        if (tabsCtl != nullptr) {
            for (int i = 0; i < static_cast<int>(tabCount); ++i) {
                wchar_t label[128]{};
                TCITEMW item{};
                item.mask = TCIF_TEXT;
                item.pszText = label;
                item.cchTextMax = static_cast<int>(sizeof(label) / sizeof(label[0]));
                RECT ir{};
                ++g_checks;
                if (::SendMessageW(tabsCtl, TCM_GETITEMW, static_cast<WPARAM>(i),
                                   reinterpret_cast<LPARAM>(&item)) == FALSE ||
                    ::SendMessageW(tabsCtl, TCM_GETITEMRECT, static_cast<WPARAM>(i),
                                   reinterpret_cast<LPARAM>(&ir)) == FALSE) {
                    continue;
                }
                const int need = textWidth(tabsCtl, label);
                if (need + 8 > static_cast<int>(ir.right - ir.left)) {
                    findings.push_back({"tab_header",
                        "tab " + std::to_string(i) + " needs " + std::to_string(need) +
                        "px in " + std::to_string(static_cast<int>(ir.right - ir.left)) + "px"});
                }
            }
        }

        // ---- screenshot + JSON ---------------------------------------------
        int capW = 0;
        int capH = 0;
        const std::wstring shot = toWide(outDir) + L"\\tab" + std::to_wstring(tab) + L".png";
        const bool shotOk = captureWindow(dlg, shot, &capW, &capH);

        for (const Finding& f : findings) { noteKind(f.kind); }
        g_findings += static_cast<int>(findings.size());
        json += "  {\"tab\": " + std::to_string(tab) + ", \"controls\": " +
                std::to_string(ctls.size()) + ", \"dialog\": [" +
                std::to_string(client.right) + "," + std::to_string(client.bottom) +
                "], \"page\": [" + std::to_string(page.left) +
                "," + std::to_string(page.top) + "," + std::to_string(page.right) + "," +
                std::to_string(page.bottom) + "], \"screenshot\": \"" +
                (shotOk ? "tab" + std::to_string(tab) + ".png" : "") + "\", \"findings\": [";
        for (std::size_t i = 0; i < findings.size(); ++i) {
            json += std::string(i > 0 ? ", " : "") + std::string("{\"kind\": \"") +
                    findings[i].kind + "\", \"detail\": \"" +
                    jsonEscape(findings[i].detail) + "\"}";
        }
        json += "], \"controls_detail\": [";
        for (std::size_t i = 0; i < ctls.size(); ++i) {
            const Ctl& c = ctls[i];
            json += std::string(i > 0 ? ", " : "") + "{\"id\": " + std::to_string(c.id) +
                    ", \"class\": \"" + jsonEscape(c.klass) + "\", \"rect\": [" +
                    std::to_string(c.x) + "," + std::to_string(c.y) + "," +
                    std::to_string(c.w) + "," + std::to_string(c.h) + "], \"fontH\": " +
                    std::to_string(c.fontHeight) + ", \"text\": \"" +
                    jsonEscape(c.text.substr(0, 120)) + "\"}";
        }
        json += "]}";
        json += tab + 1 < static_cast<int>(tabCount) ? ",\n" : "\n";

        std::printf("  tab %d: %d controls, %d findings%s (page %d,%d..%d,%d dialog %dx%d)\n",
                    tab, static_cast<int>(ctls.size()), static_cast<int>(findings.size()),
                    shotOk ? "" : " (screenshot failed)", static_cast<int>(page.left),
                    static_cast<int>(page.top), static_cast<int>(page.right),
                    static_cast<int>(page.bottom), static_cast<int>(client.right),
                    static_cast<int>(client.bottom));
        for (const Finding& f : findings) {
            std::printf("    [%s] %s\n", f.kind.c_str(), f.detail.c_str());
            if (f.kind == "overlap" || f.kind == "hittest") { std::printf("\n"); }
        }
    }
    json += " ],\n \"controls\": " + std::to_string(totalControls) + ",\n \"checks\": " +
            std::to_string(g_checks) + ",\n \"findings\": " + std::to_string(g_findings) +
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
