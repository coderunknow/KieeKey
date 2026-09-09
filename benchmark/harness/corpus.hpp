//============================================================================
// Vietnamese IME cross-engine benchmark — harness
// SPDX-License-Identifier: GPL-3.0-or-later
//
// File: benchmark/harness/corpus.hpp
//----------------------------------------------------------------------------
// Streams: the exact key events every engine is fed. One parse, shared by all
// engines, so a difference in output can only come from the engine.
//============================================================================
#pragma once

#include "viet.hpp"

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace corpus {

//============================================================================
// Key events
//============================================================================
enum class Kind : uint8_t { Char = 0, Space, Backspace, WordBreak };

struct Event {
    Kind kind = Kind::Char;
    char32_t ch = 0;      // the character the key produces (for display/passthrough)
    bool caps = false;    // Shift held
};

// Stream-language parser ('^'=shift+letter, '~'=shift+symbol, ' '=space,
// '#'=backspace, '%'=word break). Every engine consumes the same vector.
inline std::vector<Event> parse(const std::string& stream) {
    std::vector<Event> ev;
    ev.reserve(stream.size());
    for (std::size_t i = 0; i < stream.size(); ++i) {
        const char c = stream[i];
        Event e;
        if (c == '^' && i + 1 < stream.size()) {
            e.ch = static_cast<char32_t>(stream[++i]);
            e.caps = true;
        } else if (c == '~' && i + 1 < stream.size()) {
            e.ch = static_cast<char32_t>(stream[++i]);   // shifted symbol
            e.caps = true;
        } else if (c == ' ') {
            e.kind = Kind::Space;
            e.ch = U' ';
        } else if (c == '#') {
            e.kind = Kind::Backspace;
        } else if (c == '%') {
            e.kind = Kind::WordBreak;
            e.ch = U'\r';
        } else {
            e.ch = static_cast<char32_t>(static_cast<unsigned char>(c));
        }
        ev.push_back(e);
    }
    return ev;
}

// Symbol char -> the VK the real front-end would report (Shift+base key).
// Letters/digits pass through unchanged.
inline uint16_t vkForSymbol(char32_t c) {
    switch (c) {
        case U',': return 188;               // VK_OEM_COMMA
        case U'.': return 190;               // VK_OEM_PERIOD
        case U'?': return 191;               // Shift+/
        case U'/': return 191;
        case U';': case U':': return 186;    // VK_OEM_1
        case U'\'': case U'"': return 222;   // VK_OEM_7
        case U'[': case U'{': return 219;
        case U']': case U'}': return 221;
        case U'-': case U'_': return 189;
        case U'=': case U'+': return 187;
        case U'!': return 0x31;              // Shift+1
        case U'@': return 0x32;
        case U'#': return 0x33;
        case U'$': return 0x34;
        case U'%': return 0x35;
        case U'^': return 0x36;
        case U'&': return 0x37;
        case U'*': return 0x38;
        case U'(': return 0x39;
        case U')': return 0x30;
        case U'~': return 192;                // VK_OEM_3
        default: return static_cast<uint16_t>(c);
    }
}

// The character a pass-through of this key should insert into the app.
inline char32_t displayChar(const Event& e) {
    if (e.kind == Kind::Backspace) { return U'\b'; }
    if (e.kind == Kind::WordBreak) { return U'\r'; }
    if (e.kind == Kind::Space) { return U' '; }
    if (e.caps && e.ch >= U'a' && e.ch <= U'z') { return e.ch - 32; }
    return e.ch;
}

inline bool isLetterKey(const Event& e) {
    if (e.kind != Kind::Char) { return false; }
    const char32_t c = e.ch;
    return (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z') ||
           viet::isVietLetter(c);
}

//============================================================================
// Corpus: real Vietnamese words (the repo's 73,901-entry list), filtered to
// single tokens the encoder can model. The filter rule is published in the
// report so the corpus is auditable and reproducible.
//============================================================================
struct WordCorpus {
    std::vector<std::string> words;      // intended text, UTF-8
    std::vector<std::string> keys;       // keystroke stream for that word
    uint64_t totalLines = 0;
    uint64_t skippedSpace = 0;
    uint64_t skippedLong = 0;
    uint64_t skippedEncode = 0;

    // Filter: 1..14 code points, letters only (Vietnamese + ASCII), no space,
    // no digit, no hyphen in the primary set (hyphenated forms are a separate
    // category so a rule difference cannot distort the headline number).
    std::size_t load(const std::string& path, std::size_t maxWords, bool allowHyphen,
                     const viet::Encoder& enc) {
        std::ifstream f(path);
        if (!f) { return 0; }
        std::string line;
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') { line.pop_back(); }
            if (line.empty()) { continue; }
            ++totalLines;
            const std::u32string t = viet::utf8To32(line);
            if (t.size() > 14) { ++skippedLong; continue; }
            bool bad = false;
            bool hasHyphen = false;
            for (char32_t cp : t) {
                if (cp == U'-') { hasHyphen = true; continue; }
                if (cp == U' ') { bad = true; break; }
                if (cp >= U'0' && cp <= U'9') { bad = true; break; }
                if (viet::isVietLetter(cp)) { continue; }
                if ((cp >= U'a' && cp <= U'z') || (cp >= U'A' && cp <= U'Z')) { continue; }
                bad = true;
                break;
            }
            if (bad) { ++skippedSpace; continue; }
            if (hasHyphen && !allowHyphen) { ++skippedEncode; continue; }
            std::string stream;
            if (!enc.encode(line, stream)) { ++skippedEncode; continue; }
            words.push_back(line);
            keys.push_back(stream);
            if (words.size() >= maxWords) { break; }
        }
        return words.size();
    }
};

//============================================================================
// Passages: human-authored target text (the intended result of typing).
// Sentence set is fixed here in source, covering the rule families that
// separate engines: tone placement (oa/oe/uy families), ư/ơ/ă, d-gi-đ,
// foreign/loan words where auto-restore policy shows up, digits, punctuation,
// sentence-initial capitals, and code-switched English.
//============================================================================
struct Passage { const char* name; const char* intended; };

inline const std::vector<Passage>& passages() {
    static const std::vector<Passage> kP = {
        {"p01-greeting",  "Xin chào, tôi tên là Nam. Rất vui được gặp bạn!"},
        {"p02-weather",   "Hôm nay trời đẹp quá, chúng ta đi chơi nhé!"},
        {"p03-place",     "Tôi đang học tiếng Việt ở Cần Thơ."},
        {"p04-thanks",    "Cảm ơn bạn đã giúp đỡ tôi rất nhiều."},
        {"p05-question",  "Bạn có khỏe không? Tôi khỏe, cảm ơn!"},
        {"p06-work",      "Công việc hôm nay nhiều quá, tôi phải làm thêm giờ."},
        {"p07-food",      "Món ăn này ngon quá, bạn nấu giỏi thật đấy!"},
        {"p08-meeting",   "Ngày mai chúng ta họp lúc 9 giờ sáng nhé."},
        {"p09-report",    "Tôi sẽ gửi báo cáo cho bạn trước cuối tuần."},
        {"p10-family",    "Em yêu anh nhiều lắm, anh có nhớ em không?"},
        {"p11-code-switch","Please check the file tôi đã gửi qua email nhé."},
        {"p12-loanword",  "OK, tôi sẽ xác nhận lại với team rồi báo bạn sau."},
        {"p13-phone",      "Số điện thoại của tôi là 0901234567, gọi cho tôi nhé."},
        {"p14-money",     "Giá sản phẩm là 1.250.000 đồng, chưa gồm thuế."},
        {"p15-date",      "Hẹn gặp bạn lúc 14:30 ngày 15/08/2026 nhé."},
        {"p16-uoy-family","Hòa nhập với khu phố, hóa ra quỹ hỗ trợ thủy lợi đều đủ."},
        {"p17-uy-ye",     "Chú ý: kỵ sĩ ấy về quê, pyjama của anh ấy để ở kệ."},
        {"p19-questions", "Tại sao quy trình này yêu cầu kiểm tra kỹ lưỡng thế?"},
        {"p20-work2",     "Kỹ sư cơ khí đang thiết kế tủ bếp cho quán cà phê."},
        {"p21-rare",      "Thuỷ thủ quỳ gối, khuỷu tay tì trên quầy bia."},
        {"p22-numbers",   "Năm 2026, dự án tiêu 15 tỷ đồng, giảm 3% chi phí."},
        {"p23-email",     "Liên hệ: nam.nguyen@cong-ty.vn hoặc gọi 0292-3831234."},
        {"p24-caps",      "Thủ tướng trả lời chất vấn tại Hội trường Ba Đình."},
        {"p25-mixed",     "UI mới dùng TypeScript, còn API thì viết bằng C++."},
    };
    return kP;
}

//============================================================================
// Stress streams: raw keystrokes (no intended text) — behaviour probes.
// '#' = backspace, so these exercise delete/undo paths and mark thrash.
//============================================================================
struct Stress { const char* name; const char* stream; };

inline const std::vector<Stress>& stressStreams() {
    static const std::vector<Stress> kS = {
        {"s01-double-tone",    "chaof"},        {"s02-double-tone2",  "chaoff"},
        {"s03-triple-tone",    "chaof#f"},      {"s04-tone-then-del", "chaof###chaof"},
        {"s05-hat-repeat",     "cass"},         {"s06-hat-repeat3",   "casss"},
        {"s07-hat-then-bs",    "caas#"},        {"s08-w-standalone",  "cf"},
        {"s09-w-after-o",      "hof"},          {"s10-ư-initial",     "uwngf"},
        {"s11-quick-dd",       "ddoo#j"},       {"s12-bs-at-empty",   "###a#"},
        {"s13-bs-across-space","tois# #f"},     {"s14-bs-storm",      "ddiimhhf###f###s##r"},
        {"s15-long-word",      "nguuuuuuuuuuuuuuuuuuuuuuuuuuuuuoi"},
        {"s16-tone-middle",    "tro#ngs"},      {"s17-brackets",      "d[][aao"},
        {"s18-ascii-syms",     "ab!cd?ef:gh\"ij"},
        {"s19-digit-in-word",  "a1b2c3d"},
        {"s20-upper-mix",      "^T^H^O^I^S"},
        {"s21-hyphen",         "ba-ren-"},
        {"s22-nonword-letters","zzzzqqqqxxxx"},
        {"s23-tie-ng",         "ngiiiieeengx"},
        {"s24-quy-tr",         "quys#ft"},
        {"s25-gi-tr",          "giingfx"},
        {"s26-restore-cycle",  "chaaof##ooj"},
        {"s27-mixed-punct",    "Ha Nooi, Viet Nam."},
        {"s28-final-break",    "ddiif%"},
        {"s29-space-x2",       "a  b   c    d"},
        {"s30-tab-enter",      "ab%cd%ef"},
    };
    return kS;
}

//============================================================================
// Latency streams — deterministic, corpus-derived, identical for every engine.
//============================================================================
struct LatencySet {
    std::vector<Event> words;      // normal prose typing
    std::vector<Event> edit;       // prose + edit storm (backspaces)
    std::vector<Event> pathological;  // long words, hat/tone thrash
};

inline std::vector<Event> chainWords(const std::vector<std::string>& keys, std::size_t nKeys,
                                     std::size_t stride, bool withBackspace) {
    std::string stream;
    stream.reserve(nKeys + 16);
    std::size_t idx = 0;
    while (stream.size() < nKeys) {
        stream += keys[idx % keys.size()];
        idx += stride;
        stream += ' ';
        if (withBackspace && (idx % 13) == 0) { stream += "##"; }
        if (withBackspace && (idx % 29) == 0) { stream += '#'; }
    }
    while (stream.size() > nKeys) { stream.pop_back(); }
    return parse(stream);
}

inline LatencySet buildLatencySet(const WordCorpus& wc, std::size_t nKeys) {
    LatencySet ls;
    ls.words = chainWords(wc.keys, nKeys, 7, false);
    ls.edit = chainWords(wc.keys, nKeys, 11, true);
    // Pathological: one 60-letter word with hats/tones typed continuously.
    std::string path;
    for (int r = 0; r < 40; ++r) {
        path += "nguuuuuuuuuuuuuuuuuuuooooooaaaf";
        path += "dasdsdfsdfsdf###";
        path += " %";
    }
    while (path.size() < nKeys) { path += "aawsrfj#"; }
    while (path.size() > nKeys) { path.pop_back(); }
    ls.pathological = parse(path);
    return ls;
}

}  // namespace corpus
