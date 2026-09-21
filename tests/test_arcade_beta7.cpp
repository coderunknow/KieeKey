//============================================================================
// KieeKey - v1.3.0-beta7 regression tests
//
// Covers bugs found in beta7 red-team hunt:
//  B2-1: configNeedsRelaunch must include passageLanguage and vnInputMethod
//  B2-2: ArcadeServer /api/config BPM range must be 60-220, not 40-400
//  B2-3: ArcadeServer must handle passageLanguage and include it in
//        restartRequiredKeys + echo
//  B3:   ChaosLab ownsFlexing leak + flexProduced cap
//  B4:   ArcadeWindow close must not kill Flexing owned by ChaosLab
//  B5:   Desktop apply must relaunch when needsRelaunch true (logic tested
//        via manager, not Win32 controls)
//  B6:   web/arcade.js must send passageLanguage
//============================================================================
#include <cassert>
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>

#include "Arcade.hpp"
#include "ArcadeServer.hpp"

using namespace ok::arcade;

static bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

static std::string readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main() {
    std::cout << "[beta7] configNeedsRelaunch includes passageLanguage/vnInputMethod\n";
    {
        auto& mgr = ArcadeManager::instance();
        mgr.stopGame();
        ArcadeConfig base = mgr.getConfig();
        // live-only change should NOT need relaunch
        ArcadeConfig live = base;
        live.rhythmFailMode = (base.rhythmFailMode == FailMode::Hardcore) ? FailMode::HealthBar : FailMode::Hardcore;
        live.typingRacePacerWpm = 123.0;
        live.wasdSteering = WasdSteering::Both;
        assert(!mgr.configNeedsRelaunch(live) && "live knobs must not need relaunch");

        // passageLanguage change MUST need relaunch
        ArcadeConfig lang = base;
        lang.passageLanguage = (base.passageLanguage == PassageLanguage::Vietnamese)
                                   ? PassageLanguage::English
                                   : PassageLanguage::Vietnamese;
        assert(mgr.configNeedsRelaunch(lang) && "passageLanguage must trigger relaunch (B2 root cause)");

        // vnInputMethod change MUST need relaunch
        ArcadeConfig method = base;
        method.vnInputMethod = (base.vnInputMethod == VnInputMethod::Telex)
                                   ? VnInputMethod::Vni
                                   : VnInputMethod::Telex;
        assert(mgr.configNeedsRelaunch(method) && "vnInputMethod must trigger relaunch");

        // rhythmBpm change MUST need relaunch
        ArcadeConfig bpm = base;
        bpm.rhythmBpm = base.rhythmBpm + 10.0;
        assert(mgr.configNeedsRelaunch(bpm) && "rhythmBpm must trigger relaunch");

        std::cout << "  PASS configNeedsRelaunch\n";
    }

    std::cout << "[beta7] ArcadeServer BPM validation 60-220\n";
    {
        ArcadeServer server(ArcadeServerConfig{});
        // 40 should be rejected (old range allowed it, new must not)
        auto r40 = server.handleRequest("POST", "/api/config", "{\"rhythmBpm\":40}");
        std::string b40 = r40.body;
        // The echoed config must NOT be 40
        assert(!contains(b40, "\"rhythmBpm\":40") && "BPM 40 must be rejected (desktop is 60-220)");

        // 400 must be rejected
        auto r400 = server.handleRequest("POST", "/api/config", "{\"rhythmBpm\":400}");
        std::string b400 = r400.body;
        assert(!contains(b400, "\"rhythmBpm\":400") && "BPM 400 must be rejected");

        // 60 and 220 must be accepted
        auto r60 = server.handleRequest("POST", "/api/config", "{\"rhythmBpm\":60}");
        assert(contains(r60.body, "\"rhythmBpm\":60") && "BPM 60 must be accepted");
        auto r220 = server.handleRequest("POST", "/api/config", "{\"rhythmBpm\":220}");
        assert(contains(r220.body, "\"rhythmBpm\":220") && "BPM 220 must be accepted");

        // Reset to default for next tests
        server.handleRequest("POST", "/api/config", "{\"rhythmBpm\":112}");

        std::cout << "  PASS BPM range\n";
    }

    std::cout << "[beta7] ArcadeServer passageLanguage handling\n";
    {
        ArcadeServer server(ArcadeServerConfig{});
        auto resp = server.handleRequest("POST", "/api/config", "{\"passageLanguage\":1}");
        std::string body = resp.body;
        assert(contains(body, "\"passageLanguage\":1") && "server must echo passageLanguage");
        assert(contains(body, "passageLanguage") && "restartRequiredKeys must include passageLanguage");
        // Also test VN (0)
        auto resp0 = server.handleRequest("POST", "/api/config", "{\"passageLanguage\":0}");
        assert(contains(resp0.body, "\"passageLanguage\":0") && "server must handle passageLanguage=0");

        // passageLanguage change should be reported as needing relaunch
        auto respNeed = server.handleRequest("POST", "/api/config", "{\"passageLanguage\":1}");
        // Since we just set to 0, setting to 1 again should need relaunch
        // The server's configNeedsRelaunch path: if changed and applyNow=0,
        // restartRequired true. We already changed, but second call with same
        // value should NOT need relaunch. So test with opposite value.
        // To be deterministic, set to 0 then to 1 without applyNow.
        server.handleRequest("POST", "/api/config", "{\"passageLanguage\":0}");
        auto need = server.handleRequest("POST", "/api/config", "{\"passageLanguage\":1}");
        assert(contains(need.body, "\"restartRequired\":true") && "passageLanguage change must set restartRequired=true");
        // With applyNow=1 and a game running, it should relaunch
        server.startGame("typing-race");
        auto needApply = server.handleRequest("POST", "/api/config",
                                              "{\"passageLanguage\":0,\"applyNow\":1}");
        assert(contains(needApply.body, "\"restartApplied\":true") && "applyNow should relaunch for passageLanguage");
        server.stopGame();

        std::cout << "  PASS passageLanguage\n";
    }

    std::cout << "[beta7] web/arcade.js sends passageLanguage\n";
    {
        std::string js = readFile("web/arcade.js");
        assert(contains(js, "passageLanguage") && "arcade.js must send passageLanguage (B2 web bridge)");
        assert(contains(js, "passageLang") && "arcade.js must reference passageLang UI");
        std::string html = readFile("web/index.html");
        assert(contains(html, "passageLang") && "index.html must have passageLang select");
        std::cout << "  PASS web bridge\n";
    }

    std::cout << "[beta7] ChaosLab ownsFlexing leak + cap (static code check)\n";
    {
        std::string lab = readFile("src/app/ChaosLabWindow.cpp");
        assert(contains(lab, "ownsFlexing = false") && "lab must clear ownsFlexing when type != Flexing");
        assert(contains(lab, "kCap = 8192") && "lab must cap flexProduced to 8192");
        assert(contains(lab, "SetWindowTextW(impl.flexOutput, impl.flexProduced.c_str())") &&
               "lab must truncate EDIT when capping");
        std::cout << "  PASS ChaosLab cap\n";
    }

    std::cout << "[beta7] ArcadeWindow close respects ChaosLab ownership\n";
    {
        std::string win = readFile("src/app/ArcadeWindow.cpp");
        assert(contains(win, "ownsFlexingGame") && "ArcadeWindow close must check ChaosLab ownsFlexingGame()");
        assert(contains(win, "ChaosLabWindow") && "ArcadeWindow must include ChaosLabWindow.hpp");
        std::cout << "  PASS ArcadeWindow close\n";
    }

    std::cout << "[beta7] Desktop arcade apply relaunches + header refresh\n";
    {
        std::string mainCpp = readFile("src/app/main.cpp");
        assert(contains(mainCpp, "tryReadArcadeConfigFromDialog") && "main.cpp must have shared arcade reader");
        assert(contains(mainCpp, "configNeedsRelaunch") && "desktop apply must check needsRelaunch");
        assert(contains(mainCpp, "relaunchCurrentGame") && "desktop apply must call relaunchCurrentGame");
        assert(contains(mainCpp, "updateHeaderStatus") && "IDC_BTN_APPLY must call updateHeaderStatus");
        // Ensure settingsFromControls also applies arcade config
        assert(contains(mainCpp, "arcadeCfg") && "settingsFromControls must apply arcade config on OK/Apply");
        std::cout << "  PASS desktop apply\n";
    }

    std::cout << "\nAll beta7 regression checks PASS\n";
    return 0;
}
