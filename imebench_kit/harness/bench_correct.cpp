//============================================================================
// KieeKey - A modified version based on OpenKey
//
// Original work:
//   OpenKey - Vietnamese input method engine
//   Copyright (C) 2019 Tuyen Mai - https://github.com/tuyenvm/OpenKey
//   Licensed under the GNU General Public License version 3.
//
// Modified work:
//   KieeKey v1.2.1 RC3 - refactored and completed logic
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
// File: imebench_kit/harness/bench_correct.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//============================================================================
//----------------------------------------------------------------------------
// imebench_kit/harness/bench_correct.cpp — round-trip typing of the corpus
// through every engine × config × method. Emits results/*.json with the raw
// output per word; scripts/analyze_correct.py scores strict + normalized.
//
// Configs (master prompt §4.1):
//   as-shipped : KieeKey restore=ON, 2.0.5 restore=OFF (its shipped
//                default), UniKey autoNonVnRestore=ON
//   matched    : restore=OFF ×3 (byte-parity check for KieeKey vs 2.0.5)
//----------------------------------------------------------------------------
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../../src/core/TextEngine.hpp"
#include "../harness/keygen.hpp"
#include "../harness/adapters.hpp"
#ifdef BENCH_WITH_205
#include "engine205.hpp"
#endif

static std::wstring loadCorpus(const char* path, std::vector<std::wstring>& words) {
    std::ifstream f(path);
    if (!f) { std::fprintf(stderr, "corpus not found: %s\n", path); return L""; }
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        // UTF-8 -> UTF-16 (BMP)
        std::wstring w;
        for (std::size_t i = 0; i < line.size();) {
            unsigned char b = static_cast<unsigned char>(line[i]);
            char32_t cp = 0; int n = 1;
            if (b < 0x80) { cp = b; }
            else if ((b >> 5) == 6) { cp = b & 0x1F; n = 2; }
            else if ((b >> 4) == 14) { cp = b & 0x0F; n = 3; }
            else { cp = b & 0x07; n = 4; }
            for (int k = 1; k < n && i + k < line.size(); ++k) {
                cp = (cp << 6) | (static_cast<unsigned char>(line[i + k]) & 0x3F);
            }
            i += n;
            if (cp <= 0xFFFF) { w.push_back(static_cast<wchar_t>(cp)); }
        }
        if (!w.empty()) { words.push_back(w); }
    }
    return L"";
}

static std::string toUtf8(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) {
        const char32_t cp = c;
        if (cp < 0x80) { s += static_cast<char>(cp); }
        else if (cp < 0x800) {
            s += static_cast<char>(0xC0 | (cp >> 6));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            s += static_cast<char>(0xE0 | (cp >> 12));
            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return s;
}

struct EngineRun {
    std::string engine, config, method;
    std::vector<std::pair<std::string, std::string>> rows;   // expected, got
};

int main(int argc, char** argv) {
    const char* corpusPath = "tests/data/viet74k.txt";
    const char* outDir = "imebench_kit/results";
    long limit = -1;   // -1 = all words
    for (int i = 1; i < argc; ++i) {
        if (!std::strncmp(argv[i], "--corpus=", 9)) corpusPath = argv[i] + 9;
        else if (!std::strncmp(argv[i], "--out=", 6)) outDir = argv[i] + 6;
        else if (!std::strncmp(argv[i], "--limit=", 8)) limit = std::atol(argv[i] + 8);
    }
    std::vector<std::wstring> words;
    loadCorpus(corpusPath, words);
    std::fprintf(stderr, "[correct] corpus: %zu words\n", words.size());

    // ---- the general Vietnamese lexicon (committed data, sorted) -------
    // Built from the corpus vocabulary — used ONLY as the restore veto
    // (a composed form that is not a word reverts to the raw keystrokes);
    // it never injects text, and the feature is OFF in the matched config.
    std::vector<std::string> lexicon;
    lexicon.reserve(words.size() * 2);
    for (const auto& w : words) {
        // split corpus lines into individual words (the lexicon is a word list)
        std::string line = toUtf8(w), tok;
        for (std::size_t i = 0; i <= line.size(); ++i) {
            if (i == line.size() || line[i] == ' ') {
                if (!tok.empty()) { lexicon.push_back(tok); tok.clear(); }
            } else { tok.push_back(line[i]); }
        }
    }
    std::sort(lexicon.begin(), lexicon.end());
    lexicon.erase(std::unique(lexicon.begin(), lexicon.end()), lexicon.end());
    std::fprintf(stderr, "[correct] lexicon: %zu word entries\n", lexicon.size());

    std::vector<EngineRun> runs;
    const char* methods[] = {"telex", "vni"};
    const int methodIds[] = {0, 1};

    for (int mi = 0; mi < 2; ++mi) {
        const int m = methodIds[mi];
        // ---- KieeKey as-shipped (restore ON) / matched (restore OFF) ----
        for (int cfg = 0; cfg < 2; ++cfg) {
            EngineRun run;
            run.engine = "kieekey";
            run.config = cfg == 0 ? "as-shipped" : "matched";
            run.method = methods[mi];
            // as-shipped v3.1 = restore ON + dictionary-assisted restore ON;
            // matched = both OFF (byte-parity with 2.0.5 preserved).
            bench::KieeKeyAdapter ad(/*restore=*/cfg == 0,
                                     /*dictionary=*/cfg == 0, &lexicon);
            ad.setMethod(m);
            for (const auto& w : words) {
                std::string keys;
                if (!keygen::wordToKeys(w, keys, m)) { continue; }   // untypeable
                ad.reset();
                for (char c : keys) {
                    if (c == ' ') { ad.space(); }
                    else { ad.key(static_cast<char32_t>(c)); }
                }
                ad.space();   // the trailing word-break of the protocol
                run.rows.emplace_back(toUtf8(w), ad.textUtf8());
            }
            runs.push_back(std::move(run));
        }
#ifdef BENCH_WITH_UNIKEY
        // ---- UniKey UKEngine: as-shipped (autoNonVnRestore=ON) / matched (=OFF) ----
        for (int cfg = 0; cfg < 2; ++cfg) {
            EngineRun run;
            run.engine = "unikey";
            run.config = cfg == 0 ? "as-shipped" : "matched";
            run.method = methods[mi];
            bench::UnikeyAdapter ad(/*autoNonVnRestore=*/cfg == 0, /*freeMarking=*/false);
            ad.setMethod(m);
            for (const auto& w : words) {
                std::string keys;
                if (!keygen::wordToKeys(w, keys, m)) { continue; }
                ad.reset();
                for (char c : keys) {
                    if (c == ' ') { ad.space(); }
                    else { ad.key(static_cast<char32_t>(c)); }
                }
                ad.space();
                run.rows.emplace_back(toUtf8(w), ad.textUtf8());
            }
            runs.push_back(std::move(run));
        }
#endif
#ifdef BENCH_WITH_205
        // ---- OpenKey 2.0.5 (restore OFF both configs; its shipped default) ----
        {
            EngineRun run;
            run.engine = "ok205";
            run.config = "as-shipped";
            run.method = methods[mi];
            ok205::Options o;
            o.method = static_cast<ok205::Method>(m);
            o.checkSpelling = true;
            o.restoreIfWrongSpelling = false;   // 2.0.5 as-shipped default
            ok205::init(o);
            std::wstring text;
            for (const auto& w : words) {
                std::string keys;
                if (!keygen::wordToKeys(w, keys, m)) { continue; }
                ok205::reset();
                text.clear();
                auto apply = [&](const ok205::Delta& d) {
                    if (d.suppressed) {
                        std::size_t bs = std::min<std::size_t>(d.backspace, text.size());
                        text.erase(text.size() - bs, bs);
                        text += d.text;
                    } else {
                        text += d.text;
                    }
                };
                for (char c : keys) {
                    ok205::Delta d;
                    if (c == ' ') { ok205::processSpace(false, d); apply(d); }
                    else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
                        ok205::processChar(static_cast<char32_t>(c), false, false, d); apply(d);
                    } else {
                        // shifted symbols as real shift+key events (word break)
                        if (ok205::processSymbol(c, d)) { apply(d); }
                    }
                }
                ok205::Delta dEnd;
                ok205::processSpace(false, dEnd);
                apply(dEnd);
                run.rows.emplace_back(toUtf8(w), toUtf8(text));
            }
            runs.push_back(std::move(run));
        }
#endif
    }

    // ---- write JSON ----
    for (const auto& r : runs) {
        std::ostringstream path;
        path << outDir << "/correct_" << r.engine << "_" << r.config << "_" << r.method << ".json";
        std::ofstream f(path.str());
        f << "{\n  \"engine\": \"" << r.engine << "\",\n  \"config\": \"" << r.config
          << "\",\n  \"method\": \"" << r.method << "\",\n  \"rows\": [\n";
        for (std::size_t i = 0; i < r.rows.size(); ++i) {
            f << "    {\"w\": \"" << r.rows[i].first << "\", \"got\": \"" << r.rows[i].second << "\"}"
              << (i + 1 < r.rows.size() ? "," : "") << "\n";
        }
        f << "  ]\n}\n";
        std::fprintf(stderr, "[correct] wrote %s (%zu rows)\n", path.str().c_str(), r.rows.size());
    }
    return 0;
}
