//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Modified work:
//   KieeKey - refactored and completed logic
//   Copyright (C) 2026 coderunknow - https://github.com/coderunknow
//   SPDX-FileCopyrightText: 2026 coderunknow <https://github.com/coderunknow>
//
// File: src/app/ArcadeWindow.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
// Win32 GDI front-end for the Arcade Hub.
//
// Everything the user sees is drawn from `ok::arcade::RenderList`, the same
// device-independent command stream the HTML5 client consumes, so the two
// front-ends cannot drift apart. The GDI mapping is intentionally mechanical:
//
//   Rect   -> RoundRect / Rectangle               (+ optional stroke)
//   Circle -> Ellipse
//   Line   -> MoveToEx / LineTo (cosmetic pen)
//   Poly   -> Polygon
//   Text   -> TextOutW with a cached HFONT (bold/mono/size variants)
//
// Double buffering: every frame is composed in a memory DC and blitted once,
// which is what keeps a 60 Hz redraw flicker-free on GDI.
//----------------------------------------------------------------------------
#include "ArcadeWindow.hpp"

#include "ArcadeHubLaunch.hpp"

#if !defined(_WIN32)
// The hub window only exists on Windows. Keeping a stub for other platforms
// means this translation unit still compiles in a cross-check build.
namespace ok::app {
struct ArcadeWindow::Impl {};
ArcadeWindow& ArcadeWindow::instance() noexcept {
    static ArcadeWindow s_instance;
    return s_instance;
}
ArcadeWindow::ArcadeWindow() = default;
bool ArcadeWindow::open(NativeWindowHandle, const std::string&) { return false; }
bool ArcadeWindow::openGame(NativeWindowHandle, const std::string&) { return false; }
void ArcadeWindow::close() {}
bool ArcadeWindow::isOpen() const noexcept { return false; }
NativeWindowHandle ArcadeWindow::handle() const noexcept { return nullptr; }
void ArcadeWindow::focus() {}
int ArcadeWindow::hoverIndexForTest() const noexcept { return -1; }
void ArcadeWindow::setHoverIndex(int) noexcept {}
void ArcadeWindow::pump(double) {}
void ArcadeWindow::paintNow(NativeWindowHandle) {}

bool launchArcadeHub(const char*) { return false; }
bool launchChaosLab() { return false; }
} // namespace ok::app

#else  // _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>   // GET_X_LPARAM / GET_Y_LPARAM (mouse hit testing)

#if defined(_MSC_VER)
// GradientFill lives in msimg32; linking it here as well as in CMakeLists keeps
// the file self-sufficient when it is compiled by hand.
#  pragma comment(lib, "comctl32.lib")   // v1.3.0: common controls (InitCommonControlsEx)
#  pragma comment(lib, "msimg32.lib")
#  pragma comment(lib, "gdi32.lib")
#  pragma comment(lib, "user32.lib")
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

#include "Arcade.hpp"
#include "ArcadeRender.hpp"
#include "Progression.hpp"

using namespace ok::arcade;

namespace ok::app {

namespace {

constexpr const wchar_t* kWindowClass = L"KieeKeyArcadeHubWindow";
constexpr const wchar_t* kWindowTitle = L"KieeKey Arcade Hub";

COLORREF toColorRef(Color color) { return RGB(colorR(color), colorG(color), colorB(color)); }

int toAlpha(Color color) { return colorA(color); }

std::wstring utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int needed = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                             static_cast<int>(utf8.size()), nullptr, 0);
    if (needed <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(needed), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), wide.data(),
                          needed);
    return wide;
}

// A GDI brush/pen/font cache keyed by color+size: a 60 Hz redraw of a game
// frame uses a few dozen distinct objects, so this removes essentially all
// object churn from the paint path.
class GdiCache {
public:
    HBRUSH brushFor(Color color) {
        const auto key = static_cast<std::uint32_t>(color);
        auto it = m_brushes.find(key);
        if (it != m_brushes.end()) {
            return it->second;
        }
        HBRUSH brush = ::CreateSolidBrush(toColorRef(color));
        m_brushes.emplace(key, brush);
        return brush;
    }

    HPEN penFor(Color color, int width) {
        const int wide = width < 1 ? 1 : (width > 255 ? 255 : width);
        const std::uint64_t key = (static_cast<std::uint64_t>(color) << 8) |
                                  static_cast<std::uint8_t>(wide);
        auto it = m_pens.find(key);
        if (it != m_pens.end()) {
            return it->second;
        }
        HPEN pen = ::CreatePen(PS_SOLID, wide, toColorRef(color));
        m_pens.emplace(key, pen);
        return pen;
    }

    HFONT fontFor(int px, bool bold, bool mono) {
        const int size = px < 8 ? 8 : (px > 200 ? 200 : px);
        const std::uint64_t key = (static_cast<std::uint64_t>(size) << 2) | (bold ? 2u : 0u) |
                                  (mono ? 1u : 0u);
        auto it = m_fonts.find(key);
        if (it != m_fonts.end()) {
            return it->second;
        }
        LOGFONTW lf{};
        lf.lfHeight = -size;
        lf.lfWeight = bold ? FW_BOLD : FW_NORMAL;
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfQuality = CLEARTYPE_QUALITY;
        lf.lfPitchAndFamily = mono ? FIXED_PITCH : VARIABLE_PITCH;
        ::wcscpy_s(lf.lfFaceName, mono ? L"Consolas" : L"Segoe UI");
        HFONT font = ::CreateFontIndirectW(&lf);
        m_fonts.emplace(key, font);
        return font;
    }

    void clear() {
        for (auto& entry : m_brushes) { ::DeleteObject(entry.second); }
        for (auto& entry : m_pens) { ::DeleteObject(entry.second); }
        for (auto& entry : m_fonts) { ::DeleteObject(entry.second); }
        m_brushes.clear();
        m_pens.clear();
        m_fonts.clear();
    }

    ~GdiCache() { clear(); }

private:
    std::unordered_map<std::uint32_t, HBRUSH> m_brushes;
    std::unordered_map<std::uint64_t, HPEN> m_pens;
    std::unordered_map<std::uint64_t, HFONT> m_fonts;
};

} // namespace

//===========================================================================
// Implementation state
//===========================================================================
struct ArcadeWindow::Impl {
    HWND hwnd = nullptr;
    HWND owner = nullptr;
    HDC memoryDc = nullptr;
    HBITMAP memoryBitmap = nullptr;
    HBITMAP previousBitmap = nullptr;
    int bitmapW = 0;
    int bitmapH = 0;
    double dpiScale = 1.0;
    GdiCache cache;
    RenderList list;
    std::chrono::steady_clock::time_point lastTick{};
    double fps = 0.0;
    int frameCount = 0;
    int idleFrames = 0;
    int hoverIndex = -1;
    std::chrono::steady_clock::time_point fpsWindowStart{};
    std::wstring toast;
    std::chrono::steady_clock::time_point toastUntil{};

    void releaseBackBuffer() {
        if (memoryDc != nullptr && previousBitmap != nullptr) {
            ::SelectObject(memoryDc, previousBitmap);
            previousBitmap = nullptr;
        }
        if (memoryBitmap != nullptr) {
            ::DeleteObject(memoryBitmap);
            memoryBitmap = nullptr;
        }
        if (memoryDc != nullptr) {
            ::DeleteDC(memoryDc);
            memoryDc = nullptr;
        }
        bitmapW = bitmapH = 0;
    }

    bool ensureBackBuffer(HDC reference, int width, int height) {
        if (width <= 0 || height <= 0) {
            return false;
        }
        if (memoryDc != nullptr && bitmapW == width && bitmapH == height) {
            return true;
        }
        releaseBackBuffer();
        memoryDc = ::CreateCompatibleDC(reference);
        if (memoryDc == nullptr) {
            return false;
        }
        memoryBitmap = ::CreateCompatibleBitmap(reference, width, height);
        if (memoryBitmap == nullptr) {
            ::DeleteDC(memoryDc);
            memoryDc = nullptr;
            return false;
        }
        previousBitmap = static_cast<HBITMAP>(::SelectObject(memoryDc, memoryBitmap));
        bitmapW = width;
        bitmapH = height;
        return true;
    }

    void showToast(const std::wstring& text) {
        toast = text;
        toastUntil = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    }
};

//===========================================================================
// GDI painting of a RenderList
//===========================================================================
namespace {

void fillRectColor(HDC dc, GdiCache& cache, int x, int y, int w, int h, Color color) {
    if (w <= 0 || h <= 0 || toAlpha(color) == 0) {
        return;
    }
    RECT rect{x, y, x + w, y + h};
    ::FillRect(dc, &rect, cache.brushFor(color));
}

// Paints the game into the device rectangle (originX, originY, width, height).
// The world is letterboxed inside that rectangle, exactly like the canvas
// client letterboxes it inside its own element.
void drawRenderList(HDC dc, GdiCache& cache, const RenderList& list, int originX, int originY,
                    int width, int height, double dpiScale) {
    const Viewport view = computeViewport(list.worldW, list.worldH, static_cast<float>(width),
                                          static_cast<float>(height));
    const auto toX = [&](float x) {
        return originX + static_cast<int>(std::lround(view.toDeviceX(x)));
    };
    const auto toY = [&](float y) {
        return originY + static_cast<int>(std::lround(view.toDeviceY(y)));
    };
    const auto toSize = [&](float units) { return static_cast<int>(std::lround(units * view.scale)); };
    const auto toPx = [&](float units, int minimum) {
        const int px = static_cast<int>(std::lround(units * view.scale * dpiScale));
        return px < minimum ? minimum : px;
    };

    // Background: flat fill, or a vertical gradient for the taller games.
    if (list.background == list.backgroundTop) {
        fillRectColor(dc, cache, originX, originY, width, height, list.background);
    } else {
        TRIVERTEX vertices[2]{};
        vertices[0].x = originX;
        vertices[0].y = originY;
        vertices[0].Red = static_cast<COLOR16>(colorR(list.backgroundTop) << 8);
        vertices[0].Green = static_cast<COLOR16>(colorG(list.backgroundTop) << 8);
        vertices[0].Blue = static_cast<COLOR16>(colorB(list.backgroundTop) << 8);
        vertices[0].Alpha = 0xFF00;
        vertices[1].x = originX + width;
        vertices[1].y = originY + height;
        vertices[1].Red = static_cast<COLOR16>(colorR(list.background) << 8);
        vertices[1].Green = static_cast<COLOR16>(colorG(list.background) << 8);
        vertices[1].Blue = static_cast<COLOR16>(colorB(list.background) << 8);
        vertices[1].Alpha = 0xFF00;
        GRADIENT_RECT gradient{0, 1};
        if (!::GradientFill(dc, vertices, 2, &gradient, 1, GRADIENT_FILL_RECT_V)) {
            fillRectColor(dc, cache, originX, originY, width, height, list.background);
        }
    }

    ::SetBkMode(dc, TRANSPARENT);

    for (const RenderCommand& cmd : list.commands) {
        switch (cmd.kind) {
            case RenderCommand::Kind::Rect: {
                const int w = toSize(cmd.w);
                const int h = toSize(cmd.h);
                if (w <= 0 || h <= 0) {
                    break;
                }
                const int x = toX(cmd.x);
                const int y = toY(cmd.y);
                const int radius = toSize(cmd.radius);
                const bool hasStroke = toAlpha(cmd.stroke) != 0 && cmd.strokeWidth > 0.0f;
                HGDIOBJ oldBrush = ::SelectObject(dc, cache.brushFor(cmd.fill));
                HGDIOBJ oldPen = ::SelectObject(
                    dc, hasStroke ? cache.penFor(cmd.stroke, toPx(cmd.strokeWidth, 1))
                                  : ::GetStockObject(NULL_PEN));
                if (radius > 0) {
                    ::RoundRect(dc, x, y, x + w, y + h, radius * 2, radius * 2);
                } else {
                    ::Rectangle(dc, x, y, x + w, y + h);
                }
                ::SelectObject(dc, oldPen);
                ::SelectObject(dc, oldBrush);
                break;
            }
            case RenderCommand::Kind::Circle: {
                const int radius = toSize(cmd.w);
                if (radius <= 0) {
                    break;
                }
                const int cx = toX(cmd.x);
                const int cy = toY(cmd.y);
                const bool hasStroke = toAlpha(cmd.stroke) != 0 && cmd.strokeWidth > 0.0f;
                HGDIOBJ oldBrush = ::SelectObject(dc, cache.brushFor(cmd.fill));
                HGDIOBJ oldPen = ::SelectObject(
                    dc, hasStroke ? cache.penFor(cmd.stroke, toPx(cmd.strokeWidth, 1))
                                  : ::GetStockObject(NULL_PEN));
                ::Ellipse(dc, cx - radius, cy - radius, cx + radius, cy + radius);
                ::SelectObject(dc, oldPen);
                ::SelectObject(dc, oldBrush);
                break;
            }
            case RenderCommand::Kind::Line: {
                if (toAlpha(cmd.stroke) == 0) {
                    break;
                }
                HGDIOBJ oldPen =
                    ::SelectObject(dc, cache.penFor(cmd.stroke, toPx(cmd.strokeWidth, 1)));
                ::MoveToEx(dc, toX(cmd.x), toY(cmd.y), nullptr);
                ::LineTo(dc, toX(cmd.w), toY(cmd.h));
                ::SelectObject(dc, oldPen);
                break;
            }
            case RenderCommand::Kind::Poly: {
                if (cmd.pointCount < 2) {
                    break;
                }
                POINT points[PolyShape::kMaxPoints];
                for (std::uint8_t i = 0; i < cmd.pointCount; ++i) {
                    points[i].x = toX(cmd.xs[i]);
                    points[i].y = toY(cmd.ys[i]);
                }
                const bool hasStroke = toAlpha(cmd.stroke) != 0 && cmd.strokeWidth > 0.0f;
                HGDIOBJ oldBrush = ::SelectObject(dc, cache.brushFor(cmd.fill));
                HGDIOBJ oldPen = ::SelectObject(
                    dc, hasStroke ? cache.penFor(cmd.stroke, toPx(cmd.strokeWidth, 1))
                                  : ::GetStockObject(NULL_PEN));
                ::Polygon(dc, points, static_cast<int>(cmd.pointCount));
                ::SelectObject(dc, oldPen);
                ::SelectObject(dc, oldBrush);
                break;
            }
            case RenderCommand::Kind::Text: {
                if (cmd.text.empty() || toAlpha(cmd.fill) == 0) {
                    break;
                }
                const std::wstring wide = utf8ToWide(cmd.text);
                if (wide.empty()) {
                    break;
                }
                HGDIOBJ oldFont =
                    ::SelectObject(dc, cache.fontFor(toPx(cmd.size, 8), cmd.bold, cmd.mono));
                ::SetTextColor(dc, toColorRef(cmd.fill));
                int x = toX(cmd.x);
                const int y = toY(cmd.y);
                if (cmd.align != TextAlign::Left) {
                    SIZE extent{};
                    ::GetTextExtentPoint32W(dc, wide.c_str(), static_cast<int>(wide.size()),
                                            &extent);
                    x -= (cmd.align == TextAlign::Center) ? extent.cx / 2 : extent.cx;
                }
                // The canvas client draws text vertically centred on the given
                // baseline point; GDI's TextOut anchors at the top-left, so the
                // half line height is subtracted to match the web renderer.
                TEXTMETRICW metrics{};
                ::GetTextMetricsW(dc, &metrics);
                ::SetTextAlign(dc, TA_LEFT | TA_NOUPDATECP);
                ::TextOutW(dc, x, y - metrics.tmHeight / 2, wide.c_str(),
                           static_cast<int>(wide.size()));
                ::SelectObject(dc, oldFont);
                break;
            }
        }
    }
}

//---------------------------------------------------------------------------
// Window chrome: the game list on the left, the control hint at the bottom.
// Drawn with the same cached GDI objects as the game, and hit-tested by the
// mouse handler, so the hub is fully usable without a keyboard.
//---------------------------------------------------------------------------
constexpr int kRowHeight = 48;
constexpr int kFirstRowY = 112;
constexpr Color kSidebarBg = 0x0B1220FFu;
constexpr Color kSidebarBgTop = 0x131C2EFFu;
constexpr Color kRowHover = 0x1E293BFFu;
constexpr Color kRowActive = 0x14532DFFu;
constexpr Color kAccent = 0x22C55EFFu;
constexpr Color kTextPrimary = 0xF1F5F9FFu;
constexpr Color kTextMuted = 0x94A3B8FFu;
constexpr Color kFooterBg = 0x0B1220FFu;

int hitTestCatalog(int x, int y, int width, int height, double dpiScale) {
    const int sidebar = static_cast<int>(ArcadeWindow::kSidebarWidth * dpiScale);
    const int footer = static_cast<int>(ArcadeWindow::kFooterHeight * dpiScale);
    if (x < 0 || x >= sidebar || y < 0 || y >= height - footer) {
        return -1;
    }
    const auto& catalog = gameCatalog();
    const int rowHeight = static_cast<int>(kRowHeight * dpiScale);
    const int firstRow = static_cast<int>(kFirstRowY * dpiScale);
    for (std::size_t i = 0; i < catalog.size(); ++i) {
        const int top = firstRow + static_cast<int>(i) * rowHeight;
        if (y >= top && y < top + rowHeight - 4) {
            return static_cast<int>(i);
        }
    }
    (void)width;
    return -1;
}

void drawChrome(HDC dc, GdiCache& cache, const RenderList& list, int width, int height,
                double dpiScale, int hoverIndex, int selectedIndex) {
    const auto px = [&](double units) { return static_cast<int>(std::lround(units * dpiScale)); };
    const int sidebar = px(ArcadeWindow::kSidebarWidth);
    const int footer = px(ArcadeWindow::kFooterHeight);

    // ---- sidebar ----
    TRIVERTEX vertices[2]{};
    vertices[0].x = 0;
    vertices[0].y = 0;
    vertices[0].Red = static_cast<COLOR16>(colorR(kSidebarBgTop) << 8);
    vertices[0].Green = static_cast<COLOR16>(colorG(kSidebarBgTop) << 8);
    vertices[0].Blue = static_cast<COLOR16>(colorB(kSidebarBgTop) << 8);
    vertices[0].Alpha = 0xFF00;
    vertices[1].x = sidebar;
    vertices[1].y = height - footer;
    vertices[1].Red = static_cast<COLOR16>(colorR(kSidebarBg) << 8);
    vertices[1].Green = static_cast<COLOR16>(colorG(kSidebarBg) << 8);
    vertices[1].Blue = static_cast<COLOR16>(colorB(kSidebarBg) << 8);
    vertices[1].Alpha = 0xFF00;
    GRADIENT_RECT gradient{0, 1};
    if (!::GradientFill(dc, vertices, 2, &gradient, 1, GRADIENT_FILL_RECT_V)) {
        fillRectColor(dc, cache, 0, 0, sidebar, height - footer, kSidebarBg);
    }

    ::SetBkMode(dc, TRANSPARENT);
    ::SetTextAlign(dc, TA_LEFT | TA_NOUPDATECP);

    HGDIOBJ oldFont = ::SelectObject(dc, cache.fontFor(px(19), true, false));
    ::SetTextColor(dc, toColorRef(kAccent));
    const wchar_t* title = L"ARCADE HUB";
    ::TextOutW(dc, px(20), px(20), title, static_cast<int>(::wcslen(title)));
    ::SelectObject(dc, oldFont);

    oldFont = ::SelectObject(dc, cache.fontFor(px(12), false, false));
    ::SetTextColor(dc, toColorRef(kTextMuted));
    const wchar_t* subtitle = L"8 mini-game gõ phím · chọn để chơi";
    ::TextOutW(dc, px(20), px(48), subtitle, static_cast<int>(::wcslen(subtitle)));
    ::SelectObject(dc, oldFont);

    const auto& catalog = gameCatalog();
    const int rowHeight = px(kRowHeight);
    for (std::size_t i = 0; i < catalog.size(); ++i) {
        const GameInfo& info = catalog[i];
        const int top = px(kFirstRowY) + static_cast<int>(i) * rowHeight;
        const bool active = (static_cast<int>(info.id) == selectedIndex);
        const bool hovered = (static_cast<int>(i) == hoverIndex);
        if (active) {
            fillRectColor(dc, cache, 0, top, px(4), rowHeight - px(4), kAccent);
            fillRectColor(dc, cache, px(4), top, sidebar - px(4), rowHeight - px(4), kRowActive);
        } else if (hovered) {
            fillRectColor(dc, cache, px(4), top, sidebar - px(4), rowHeight - px(4), kRowHover);
        }

        const std::wstring emoji = utf8ToWide(info.emoji != nullptr ? info.emoji : "");
        const std::wstring name = utf8ToWide(info.nameVi != nullptr ? info.nameVi : info.slug);
        oldFont = ::SelectObject(dc, cache.fontFor(px(18), false, false));
        ::SetTextColor(dc, toColorRef(kTextPrimary));
        ::TextOutW(dc, px(16), top + px(12), emoji.c_str(), static_cast<int>(emoji.size()));
        ::SelectObject(dc, oldFont);

        oldFont = ::SelectObject(dc, cache.fontFor(px(15), active, false));
        ::SetTextColor(dc, toColorRef(active ? kTextPrimary : kTextMuted));
        ::TextOutW(dc, px(48), top + px(14), name.c_str(), static_cast<int>(name.size()));
        ::SelectObject(dc, oldFont);

        oldFont = ::SelectObject(dc, cache.fontFor(px(11), false, false));
        ::SetTextColor(dc, toColorRef(kAccent));
        const std::wstring kind = info.typingDriven ? L"gõ phím" : L"điều khiển";
        ::TextOutW(dc, px(48), top + px(30), kind.c_str(), static_cast<int>(kind.size()));
        ::SelectObject(dc, oldFont);
    }

    // ---- footer ----
    fillRectColor(dc, cache, 0, height - footer, width, footer, kFooterBg);
    std::wstring hint = utf8ToWide(!list.hint.empty() ? list.hint : list.status);
    oldFont = ::SelectObject(dc, cache.fontFor(px(14), false, false));
    ::SetTextColor(dc, toColorRef(kTextPrimary));
    if (!hint.empty()) {
        ::TextOutW(dc, sidebar + px(20), height - footer + px(12), hint.c_str(),
                   static_cast<int>(hint.size()));
    }
    oldFont = ::SelectObject(dc, cache.fontFor(px(12), false, true));
    ::SetTextColor(dc, toColorRef(kTextMuted));
    wchar_t meta[128]{};
    std::swprintf(meta, std::size(meta), L"điểm %lld · %.0f WPM · %.1f%% chính xác",
                  static_cast<long long>(list.stats.score), list.stats.wpm,
                  list.stats.accuracy);
    ::TextOutW(dc, sidebar + px(20), height - footer + px(38), meta,
               static_cast<int>(::wcslen(meta)));
    ::SelectObject(dc, oldFont);
}

bool isModifierKey(int vk) noexcept {
    switch (vk) {
        case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
        case VK_MENU: case VK_LMENU: case VK_RMENU:
        case VK_LWIN: case VK_RWIN:
        case VK_CAPITAL: case VK_NUMLOCK: case VK_SCROLL:
            return true;
        default:
            return false;
    }
}

// The character a key press produces under the CURRENT keyboard state
// (Shift/Caps/diacritic layout). Returns 0 for keys that produce none
// (arrows, Esc, F-keys, Ctrl chords) — those reach a game through `vk`.
char32_t translateKeyChar(int vk, LPARAM lParam) noexcept {
    const UINT scanCode = static_cast<UINT>((static_cast<std::uintptr_t>(lParam) >> 16) & 0xFF);
    BYTE keyboardState[256]{};
    if (::GetKeyboardState(keyboardState) == FALSE) {
        return 0;
    }
    wchar_t buffer[8]{};
    const int count = ::ToUnicode(static_cast<UINT>(vk), scanCode, keyboardState, buffer,
                                  static_cast<int>(std::size(buffer)), 0);
    if (count <= 0) {
        return 0;   // no translation, or a dead key (negative count)
    }
    const char32_t ch = static_cast<char32_t>(buffer[0]);
    if (ch < 0x20 || ch == 0x7F) {
        return 0;   // control characters are not game input
    }
    return ch;
}

} // namespace

//===========================================================================
// Window procedure
//===========================================================================
namespace {

LRESULT CALLBACK arcadeWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    ArcadeWindow* self = reinterpret_cast<ArcadeWindow*>(
        ::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (message) {
        case WM_NCCREATE: {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                                reinterpret_cast<LONG_PTR>(create->lpCreateParams));
            return TRUE;
        }
        case WM_ERASEBKGND:
            return 1;   // fully painted in WM_PAINT (no flicker)
        case WM_PAINT:
            ArcadeWindow::paintNow(static_cast<NativeWindowHandle>(hwnd));
            return 0;
        case WM_TIMER:
            if (self != nullptr && wParam == static_cast<WPARAM>(ArcadeWindow::kTimerId)) {
                self->pump(0.0);   // dt is measured internally
                return 0;
            }
            break;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (self != nullptr) {
                const int vk = static_cast<int>(wParam);
                // Modifiers are not game input (the IME hook path drops them
                // too) — Shift/Ctrl/Alt/Win must never reach a game.
                if (!isModifierKey(vk)) {
                    InputEvent event{};
                    event.vk = vk;
                    // The character is resolved here, with the real keyboard
                    // state, so exactly ONE InputEvent is produced per physical
                    // key press. WM_CHAR below is swallowed: forwarding it as
                    // well would feed every keystroke to the game twice.
                    event.ch = translateKeyChar(vk, lParam);
                    event.down = true;
                    (void)ArcadeManager::instance().handleKey(event);
                    ::InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;
        case WM_CHAR:
        case WM_SYSCHAR:
            // Already delivered together with its WM_KEYDOWN (see above).
            return 0;
        case WM_KEYUP:
            if (self != nullptr) {
                const int vk = static_cast<int>(wParam);
                if (!isModifierKey(vk)) {
                    InputEvent event{};
                    event.vk = vk;
                    event.down = false;
                    (void)ArcadeManager::instance().handleKey(event);
                }
            }
            return 0;
        case WM_MOUSEMOVE:
            if (self != nullptr) {
                RECT client{};
                ::GetClientRect(hwnd, &client);
                const int hover = hitTestCatalog(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam),
                                                 client.right, client.bottom, 1.0);
                if (hover != self->hoverIndexForTest()) {
                    self->setHoverIndex(hover);
                    ::InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;
        case WM_LBUTTONDOWN: {
            if (self == nullptr) {
                return 0;
            }
            RECT client{};
            ::GetClientRect(hwnd, &client);
            const int index = hitTestCatalog(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam),
                                             client.right, client.bottom, 1.0);
            if (index >= 0) {
                const auto& catalog = gameCatalog();
                if (static_cast<std::size_t>(index) < catalog.size()) {
                    ArcadeManager::instance().launchGame(
                        static_cast<GameType>(catalog[static_cast<std::size_t>(index)].id));
                    ::SetFocus(hwnd);
                    ::InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;
        }
        case WM_SETCURSOR:
            if (self != nullptr && LOWORD(lParam) == HTCLIENT) {
                ::SetCursor(::LoadCursorW(nullptr, IDC_ARROW));
                return TRUE;
            }
            break;
        case WM_GETDLGCODE:
            return DLGC_WANTALLKEYS | DLGC_WANTARROWS | DLGC_WANTCHARS;
        case WM_SIZE:
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_CLOSE:
            if (self != nullptr) {
                self->close();
            }
            return 0;
        case WM_DESTROY:
            ::KillTimer(hwnd, ArcadeWindow::kTimerId);
            return 0;
        default:
            break;
    }
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace

//===========================================================================
// ArcadeWindow
//===========================================================================
ArcadeWindow& ArcadeWindow::instance() noexcept {
    static ArcadeWindow s_instance;
    return s_instance;
}

ArcadeWindow::ArcadeWindow() : m_impl(new Impl()) {}

bool ArcadeWindow::open(NativeWindowHandle owner, const std::string& slugOrEmpty) {
    if (m_impl == nullptr) {
        return false;
    }
    m_impl->owner = static_cast<HWND>(owner);

    if (m_impl->hwnd == nullptr) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wc.lpfnWndProc = arcadeWndProc;
        wc.hInstance = ::GetModuleHandleW(nullptr);
        wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = kWindowClass;
        if (::RegisterClassExW(&wc) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }

        RECT desired{0, 0, kDefaultWidth, kDefaultHeight};
        ::AdjustWindowRectEx(&desired, WS_OVERLAPPEDWINDOW, FALSE, 0);
        m_impl->hwnd = ::CreateWindowExW(
            0, kWindowClass, kWindowTitle, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT,
            CW_USEDEFAULT, desired.right - desired.left, desired.bottom - desired.top,
            m_impl->owner, nullptr, ::GetModuleHandleW(nullptr), this);
        if (m_impl->hwnd == nullptr) {
            return false;
        }

        // DPI awareness: text and strokes scale, layout maths does not.
        HDC screen = ::GetDC(nullptr);
        if (screen != nullptr) {
            const int dpi = ::GetDeviceCaps(screen, LOGPIXELSX);
            m_impl->dpiScale = (dpi > 0) ? (static_cast<double>(dpi) / 96.0) : 1.0;
            ::ReleaseDC(nullptr, screen);
        }

        ::SetTimer(m_impl->hwnd, kTimerId, kTimerIntervalMs, nullptr);
        m_impl->lastTick = std::chrono::steady_clock::now();
        m_impl->fpsWindowStart = m_impl->lastTick;
        m_impl->showToast(L"Chọn game ở menu hoặc khung bên trái để bắt đầu");
    }

    if (!slugOrEmpty.empty()) {
        const int typeId = gameTypeFromSlug(slugOrEmpty);
        if (typeId > 0) {
            ArcadeManager::instance().launchGame(static_cast<GameType>(typeId));
        }
    }

    ::ShowWindow(m_impl->hwnd, SW_SHOW);
    ::SetForegroundWindow(m_impl->hwnd);
    ::SetFocus(m_impl->hwnd);
    ::InvalidateRect(m_impl->hwnd, nullptr, FALSE);
    return true;
}

bool ArcadeWindow::openGame(NativeWindowHandle owner, const std::string& slug) {
    return open(owner, slug);
}

void ArcadeWindow::close() {
    if (m_impl == nullptr) {
        return;
    }
    // stopGame() reports the finished/abandoned run to the progression engine.
    ArcadeManager::instance().stopGame();
    if (m_impl->hwnd != nullptr) {
        HWND hwnd = m_impl->hwnd;
        m_impl->hwnd = nullptr;
        ::KillTimer(hwnd, kTimerId);
        ::DestroyWindow(hwnd);
    }
    m_impl->releaseBackBuffer();
    m_impl->cache.clear();
}

bool ArcadeWindow::isOpen() const noexcept {
    return m_impl != nullptr && m_impl->hwnd != nullptr && ::IsWindow(m_impl->hwnd) != FALSE;
}

NativeWindowHandle ArcadeWindow::handle() const noexcept {
    return (m_impl != nullptr) ? static_cast<NativeWindowHandle>(m_impl->hwnd) : nullptr;
}

int ArcadeWindow::hoverIndexForTest() const noexcept {
    return (m_impl != nullptr) ? m_impl->hoverIndex : -1;
}

void ArcadeWindow::setHoverIndex(int index) noexcept {
    if (m_impl != nullptr) {
        m_impl->hoverIndex = index;
    }
}

void ArcadeWindow::focus() {
    if (isOpen()) {
        ::SetForegroundWindow(m_impl->hwnd);
        ::SetFocus(m_impl->hwnd);
    }
}

void ArcadeWindow::pump(double dtSeconds) {
    if (m_impl == nullptr || m_impl->hwnd == nullptr || ::IsWindow(m_impl->hwnd) == FALSE) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    double dt = dtSeconds;
    if (dt <= 0.0) {
        dt = std::chrono::duration<double>(now - m_impl->lastTick).count();
    }
    m_impl->lastTick = now;
    if (dt < 0.0) {
        dt = 0.0;
    }
    if (dt > 0.25) {
        dt = 0.25;
    }

    ArcadeManager& manager = ArcadeManager::instance();
    manager.update(dt);
    // Credit finished runs immediately: the toast below is what makes the
    // level-up visible while playing, and it must not wait for the settings
    // dialog to be open.
    manager.drainRunResultsToProgression();

    // Progression feedback: a level-up is surfaced as a native toast, so
    // "gõ nhiều để lên cấp" is visible while playing.
    std::uint32_t newLevel = 0;
    if (ok::progression::ProgressionEngine::instance().consumeLevelUp(newLevel)) {
        m_impl->showToast(L"Lên cấp " + std::to_wstring(newLevel) + L"!");
    }

    if (manager.hasActiveGame()) {
        ::InvalidateRect(m_impl->hwnd, nullptr, FALSE);
    } else if (++m_impl->idleFrames >= (kTimerIntervalMs >= 16 ? 32 : 16)) {
        // No game running: the window still shows the catalog and the last
        // score, so repaint twice a second instead of 60 times.
        m_impl->idleFrames = 0;
        ::InvalidateRect(m_impl->hwnd, nullptr, FALSE);
    }
}

void ArcadeWindow::paintNow(NativeWindowHandle handle) {
    HWND hwnd = static_cast<HWND>(handle);
    if (hwnd == nullptr) {
        return;
    }
    ArcadeWindow& window = instance();
    Impl* impl = window.m_impl;
    if (impl == nullptr) {
        return;
    }

    PAINTSTRUCT ps{};
    HDC dc = ::BeginPaint(hwnd, &ps);
    if (dc == nullptr) {
        return;
    }
    RECT client{};
    ::GetClientRect(hwnd, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;

    if (!impl->ensureBackBuffer(dc, width, height)) {
        ::EndPaint(hwnd, &ps);
        return;
    }

    ArcadeManager& manager = ArcadeManager::instance();
    buildRenderList(manager.getFrame(), impl->list);

    // Layout: [ sidebar | game ] over [ footer ]. The game keeps its own
    // aspect ratio inside its pane (letterboxed), never stretched.
    const int sidebar = static_cast<int>(std::lround(kSidebarWidth * impl->dpiScale));
    const int footer = static_cast<int>(std::lround(kFooterHeight * impl->dpiScale));
    const int gameX = sidebar;
    const int gameY = 0;
    const int gameW = width - sidebar > 1 ? width - sidebar : width;
    const int gameH = height - footer > 1 ? height - footer : height;
    {
        // Clip to the game pane so no command can bleed into the chrome.
        HRGN clip = ::CreateRectRgn(gameX, gameY, gameX + gameW, gameY + gameH);
        if (clip != nullptr) {
            ::SelectClipRgn(impl->memoryDc, clip);
            ::DeleteObject(clip);
        }
    }
    drawRenderList(impl->memoryDc, impl->cache, impl->list, gameX, gameY, gameW, gameH,
                   impl->dpiScale);
    ::SelectClipRgn(impl->memoryDc, nullptr);
    drawChrome(impl->memoryDc, impl->cache, impl->list, width, height, impl->dpiScale,
               impl->hoverIndex, static_cast<int>(manager.getCurrentGameType()));

    // GDI-only overlay: frame counter + transient level-up toast. The games
    // themselves never know about either.
    const auto now = std::chrono::steady_clock::now();
    impl->frameCount++;
    const double fpsWindow = std::chrono::duration<double>(now - impl->fpsWindowStart).count();
    if (fpsWindow >= 0.5) {
        impl->fps = static_cast<double>(impl->frameCount) / fpsWindow;
        impl->frameCount = 0;
        impl->fpsWindowStart = now;
    }
    ::SetBkMode(impl->memoryDc, TRANSPARENT);
    if (impl->fps > 0.0) {
        wchar_t text[32]{};
        std::swprintf(text, std::size(text), L"%.0f FPS", impl->fps);
        ::SetTextColor(impl->memoryDc, RGB(0x9A, 0xA7, 0xB8));
        HGDIOBJ oldFont = ::SelectObject(impl->memoryDc, impl->cache.fontFor(13, false, true));
        ::TextOutW(impl->memoryDc, width - 76, height - footer - 22, text,
                   static_cast<int>(::wcslen(text)));
        ::SelectObject(impl->memoryDc, oldFont);
    }
    if (!impl->toast.empty() && now < impl->toastUntil) {
        ::SetTextColor(impl->memoryDc, RGB(0x51, 0xE8, 0x8A));
        HGDIOBJ oldFont = ::SelectObject(impl->memoryDc, impl->cache.fontFor(20, true, false));
        ::TextOutW(impl->memoryDc, 20, 12, impl->toast.c_str(),
                   static_cast<int>(impl->toast.size()));
        ::SelectObject(impl->memoryDc, oldFont);
    } else if (!impl->toast.empty()) {
        impl->toast.clear();
    }

    ::BitBlt(dc, 0, 0, width, height, impl->memoryDc, 0, 0, SRCCOPY);
    ::EndPaint(hwnd, &ps);
}

//===========================================================================
// Launch façade (see ArcadeHubLaunch.hpp)
//===========================================================================
bool launchArcadeHub(const char* slug) {
    ArcadeWindow& hub = ArcadeWindow::instance();
    return hub.open(nullptr, slug != nullptr ? std::string(slug) : std::string());
}

} // namespace ok::app

#endif  // _WIN32
