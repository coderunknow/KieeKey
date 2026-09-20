#----------------------------------------------------------------------------
# KieeKey — CMake build (modern replacement for the .sln/.vcxproj)
#
#   * C++23, /W4 + /WX (warnings as errors) for release-grade hygiene (MSVC)
#   * x64 + ARM64 out of the box (ARM64EC possible: -A ARM64EC, VS 17.4+)
#   * STATIC C/C++ runtimes on EVERY compiler:
#       - MSVC   : /MT (MultiThreaded) via CMAKE_MSVC_RUNTIME_LIBRARY
#                  (CMP0091 NEW — the flag is injected by CMake, not hacked)
#       - MinGW  : -static -static-libgcc -static-libstdc++
#     This removes the shipped-DLL dependency class entirely, including the
#     "__cxa_thread_atexit missing entry point" failure seen when a MinGW
#     build links libstdc++-6.dll from an incompatible runtime on the host.
#   * RESOURCES (fixed): the app's .rc is compiled through CMake's
#     FIRST-CLASS RC language (enable_language(RC)). CMake picks the correct
#     resource compiler per toolchain — rc.exe for MSVC, windres for
#     MinGW/GCC (including cross builds) — and wires the object into the
#     target's own dependency graph. This removes the fragile hand-rolled
#     `windres ... && target_sources(app_res.o)` PRE_LINK hack whose
#     build-graph desync produced "app_res.o not found / stale" failures
#     under Ninja and MinGW Makefiles.
#   * Portable link names (fixed): system libraries are declared as
#     bare names (`user32`, not `user32.lib`). CMake translates them per
#     toolchain (`user32.lib` for MSVC, `-luser32` for MinGW), so the same
#     declaration links under both.
#
# Configure (MSVC):
#   cmake -S . -B out/x64   -A x64
#   cmake --build out/x64   --config Release
# Configure (MinGW, native or cross — see cmake/mingw-w64-x86_64.cmake):
#   cmake -S . -B out/mingw -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake
#   cmake --build out/mingw
#----------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.28)

project(KieeKey
        VERSION 1.3.0
        DESCRIPTION "KieeKey — Vietnamese input method (modern core)"
        LANGUAGES CXX)

#----------------------------------------------------------------------------
# Options
#----------------------------------------------------------------------------
set(KIEEKEY_BUILD_UI "none" CACHE STRING "UI front-end: none | winui3")
set(KIEEKEY_BUILD_TESTS ON CACHE BOOL "Build the unit-test harness")
# Windows only: also build the extended (stress/soak/differential/keystroke)
# harnesses. OFF by default — see the note in the Windows test block for why.
set(KIEEKEY_BUILD_EXTRA_TESTS OFF CACHE BOOL "Build the extended test harnesses on Windows")
set(KIEEKEY_PROFILE 0 CACHE STRING "Compile in the QPC stage profiler (0=off/default, 1=instrumented)")
# Warnings-as-errors. ON by default (release hygiene, and what CI builds); the
# diagnostic CI build turns it OFF so that a failing build can print the FULL
# warning inventory in one pass instead of stopping at the first one per file.
set(KIEEKEY_WERROR ON CACHE BOOL "Treat compiler warnings as errors (/WX on MSVC)")
# Low-latency engine profile (PR15 port): when ON, the engine compiles with
# the grammar-repair pass and the per-key undo snapshot removed (opt-in
# behavioural trade-off — see TextEngine.hpp). Default OFF = strict build,
# behavior-identical to the pre-port engine.
set(KIEEKEY_LOW_LATENCY_PROFILE OFF CACHE BOOL "Build the low-latency engine profile (skips grammar repair + undo snapshot)")
set_property(CACHE KIEEKEY_BUILD_UI PROPERTY STRINGS none winui3)

#----------------------------------------------------------------------------
# Toolchain / standard
#----------------------------------------------------------------------------
if(MSVC)
    # /utf-8 is a COMPILE option (source/input charset): the sources and the
    # .rc carry Vietnamese UTF-8 text. It must never reach the LINK line —
    # it previously sat in target_link_options() and broke MinGW's driver
    # (g++: error: /utf-8: linker input file not found).
    # /wd4324: the alignas(64) cache-line isolation in LockFreeQueue.hpp /
    # ModernKeyHook.hpp pads structures BY DESIGN (false-sharing guard), so
    # the "structure was padded due to alignment specifier" warning is noise
    # and must not trip /WX (it fires at every template instantiation site).
    # /wd4127: the unit-test CHECK macros intentionally evaluate constant
    # conditions (e.g. CHECK(ring.capacity == 1024)) as compile-time-checked
    # regression nets; a plain `if` there fires C4127 under /W4, so it is
    # disabled too (the alternative, `if constexpr`, is ill-formed for the
    # runtime conditions the same macros also carry).
    add_compile_options(/W4 /wd4324 /wd4127 /permissive- /Zc:__cplusplus /Zc:preprocessor /utf-8)
    if(KIEEKEY_WERROR)
        add_compile_options(/WX)
    endif()
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    # /guard:cf (CFG) + ASLR — security baseline for a system hooking process
    add_link_options(/guard:cf /DYNAMICBASE)
    # STATIC C/C++ runtime (/MT for every config, /MTd for Debug) — the
    # binary must run on hosts without a matching redistributable. CMP0091
    # NEW makes CMake own the flag injection (never pass /MT by hand — it
    # would fight CMake's default /MD and break the diagnostics mapping).
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# MinGW/GCC: statically link EVERYTHING the C/C++ runtimes would normally
# provide. -static-libstdc++/-static-libgcc remove libstdc++-6.dll/
# libgcc_s_seh-1.dll dependencies (the source of the __cxa_thread_atexit
# missing-entry-point error on hosts with an older/broken runtime);
# full -static additionally drops libwinpthread-1.dll.
if(WIN32 AND NOT MSVC)
    add_link_options(-static -static-libgcc -static-libstdc++)
    # GCC 14 (MinGW-w64 posix) emits a strong definition of
    # std::type_info::operator== in every TU while libstdc++'s tinfo.o
    # carries the same symbol — the linker errors out with "multiple
    # definition". The two definitions are identical; keep the first.
    add_link_options(-Wl,--allow-multiple-definition)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

#----------------------------------------------------------------------------
# Test harnesses must ASSERT in every configuration (incl. Release/NDEBUG).
#
# The v1.3.0 harnesses (arcade, chaos, AI rival, progression, analytics,
# online ghost, soak_arcade) and outputitem verify through plain assert().
# Under CMAKE_BUILD_TYPE=Release the NDEBUG it brings compiles those
# asserts to nothing: the binaries print "[PASS]" unconditionally (and
# g++ flags the only-read-by-asserts locals as set-but-not-used). Tests
# must test in the configuration CI ships, so every TEST executable
# force-includes tests/force_asserts.hpp — which #undefs NDEBUG before
# the TU's own includes — via /FI (MSVC) / -include (GCC, Clang, MinGW).
# Attached-form flags survive paths with spaces in every generator.
#
# Deliberately NOT applied to: product targets (ok_core, ok_tsf, the app —
# release binaries stay assert-free), the frozen reference engines under
# tests/reference/ (their behavior is pinned), and the benchmark
# executables (assertion overhead would pollute the measurements).
#----------------------------------------------------------------------------
function(kieekey_test_force_asserts tgt)
    if(MSVC)
        target_compile_options(${tgt} PRIVATE "/FI${CMAKE_SOURCE_DIR}/tests/force_asserts.hpp")
    else()
        target_compile_options(${tgt} PRIVATE "-include${CMAKE_SOURCE_DIR}/tests/force_asserts.hpp")
    endif()
endfunction()

#----------------------------------------------------------------------------
# Resource compilation (Windows targets only).
# CMake's RC language is the platform-agnostic front door: on MSVC it runs
# rc.exe, on MinGW/GCC it runs windres (native or the cross toolchain's
# ${target}-windres) — the same .rc source, one rule, no hand-rolled
# PRE_LINK commands, no out-of-band .o files to desync. The codepage flag
# keeps the Vietnamese UTF-8 strings in KieeKeyApp.rc intact per compiler.
#----------------------------------------------------------------------------
if(WIN32)
    enable_language(RC)
    if(MINGW)
        set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} -c 65001")     # UTF-8 input
    elseif(MSVC)
        set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} /c 65001")     # UTF-8 input
    endif()
endif()

#----------------------------------------------------------------------------
# Windows product: core static lib + TSF composer + the KieeKeyApp.exe GUI
#----------------------------------------------------------------------------
if(WIN32)

    # Core static library — engine + hook + wrapper + monitor (no UI, testable)
    add_library(ok_core STATIC
        src/core/kieekey_core.hpp      # unified core facade
        src/core/LockFreeQueue.hpp
        src/core/CpuPause.hpp          # portable spin hint (PAUSE/YIELD)
        src/core/Win32RAII.hpp
        src/core/ModernKeyHook.hpp
        src/core/ModernKeyHook.cpp
        src/core/win32_wrapper.hpp     # transport layer (Vyukov Ring 1024,
        src/core/win32_wrapper.cpp     #  batched SendInput, self-healing watchdog)
        src/core/ProcessMonitor.hpp
        src/core/ProcessMonitor.cpp
        src/core/VietnameseTables.hpp
        src/core/TextEngine.hpp
        src/core/TextEngine.cpp
        src/core/PerfProfile.hpp       # v1.2.1 RC2: performance preference profiles
        src/core/Notifications.hpp     # v1.2.1 RC2: notification center + QuickTelex detector
        src/core/Arcade.hpp            # v1.3.0: Arcade hub framework & minigames
        src/core/Arcade.cpp
        src/core/ArcadeFrame.hpp       # v1.3.0: frame model + game catalog
        src/core/ArcadeFrame.cpp
        src/core/ArcadeRender.hpp      # v1.3.0: device-independent display list
        src/core/ArcadeRender.cpp      #  (shared by the GDI window and the web client)
        src/core/ArcadeServer.hpp      # v1.3.0: HTTP/JSON bridge + SSE frame stream
        src/core/ArcadeServer.cpp
        src/core/SeqRing.hpp           # v1.3.0: lock-free SPSC observation ring
        src/core/ChaosEngine.hpp       # v1.3.0: Chaos Case & visual glyph transforms
        src/core/ChaosEngine.cpp
        src/core/AiRival.hpp           # v1.3.0: Personal AI typing rival & simulation
        src/core/AiRival.cpp
        src/core/Progression.hpp       # v1.3.0: Global level, stats & achievements
        src/core/Progression.cpp
        src/core/TypingAnalytics.hpp   # v1.3.0: Typing metrics & intelligent coach
        src/core/TypingAnalytics.cpp
        src/core/OnlineGhost.hpp      # v1.3.0: Solo ghost, replay & leaderboard abstraction
        src/core/OnlineGhost.cpp
        src/core/Diagnostics.hpp      # v1.3.0-beta3: level-gated counters/histograms/trace
        src/core/Diagnostics.cpp      #  (beta3 shipped it with zero call sites, so the CMake
                                      #   target never noticed it was missing until beta4's
                                      #   app-side recorders LNK2019'd in MSVC CI)
    )
    target_include_directories(ok_core PUBLIC src/core)
    target_compile_features(ok_core PUBLIC cxx_std_23)
    # Bare names — CMake maps them per toolchain (user32.lib / -luser32).
    target_link_libraries(ok_core PUBLIC
        user32 gdi32 shell32 ole32 dwmapi psapi version urlmon winmm
        msimg32    # v1.3.0: GradientFill for the Arcade Hub background
        advapi32   # v1.1.3: OpenProcessToken / GetTokenInformation (elevation probe)
        wtsapi32   # v1.2.0: WTSRegisterSessionNotification (lock/unlock, RDP)
        comctl32   # v1.3.0: InitCommonControlsEx (Arcade Hub + Chaos Lab sliders)
        ws2_32)    # v1.3.0: the Arcade web bridge (ArcadeServer.cpp) uses Winsock

    # TSF composer
    add_library(ok_tsf STATIC
        src/tsf/TsfComposer.hpp
        src/tsf/TsfComposer.cpp
    )
    target_include_directories(ok_tsf PUBLIC src/tsf)
    target_link_libraries(ok_tsf PRIVATE ok_core)

    # The runnable IME application — OpenKey-style tray app (green/gray tray
    # icon, Vietnamese context menu + Cài đặt dialog; v1.1.1: on/off is an
    # in-app control — tray menu item + settings-dialog toggle button — the
    # old Ctrl+Shift global hotkey was removed).
    add_executable(openkey
        src/app/main.cpp
        src/app/ArcadeWindow.hpp   # v1.3.0: graphical Arcade Hub window (GDI)
        src/app/ArcadeWindow.cpp
        src/app/ChaosLabWindow.hpp # v1.3.0: Chaos / Flexing lab window
        src/app/ChaosLabWindow.cpp
        src/app/resource.h
        src/app/KieeKeyApp.rc      # icon(s) + manifest + version info —
                                   # compiled by the RC language rule above
    )
    # BUILD-GRAPH FIX: rc.exe/windres read KieeKeyApp.manifest and the two
    # .ico files at COMPILE time, but CMake's RC rule only tracks the .rc
    # itself — editing the manifest or icons alone produced NO recompile and
    # silently shipped STALE resources in the exe (caught by the PE audit
    # during the v1.0.1 logo refresh). Declare them as object dependencies
    # so any of the three inputs rebuilds the resource object.
    set_source_files_properties(src/app/KieeKeyApp.rc PROPERTIES
        OBJECT_DEPENDS
        "${CMAKE_SOURCE_DIR}/src/app/KieeKeyApp.manifest;${CMAKE_SOURCE_DIR}/src/app/KieeKeyApp.ico;${CMAKE_SOURCE_DIR}/src/app/KieeKeyApp-off.ico")
    set_target_properties(openkey PROPERTIES
        OUTPUT_NAME "KieeKeyApp"           # -> KieeKeyApp.exe
        EXPORT_NAME openkey)
    # INVARIANT — the app manifest exists in EXACTLY one place: embedded by
    # the RC rule from KieeKeyApp.rc as RT_MANIFEST ID 1
    # (CREATEPROCESS_MANIFEST_RESOURCE_ID — the ID the Windows loader
    # requires; never renumber it). Two facts anchor this:
    #   * MSVC's linker defaults to GenerateManifest + EmbedManifest
    #     (/MANIFEST:EMBED under the Visual Studio generator), which injects
    #     its own auto-generated manifest as a SECOND RT_MANIFEST name:1
    #     language:0x0409 -> CVTRES "CVT1100: duplicate resource.
    #     type:MANIFEST, name:1, language:0x0409" + cascade "LNK1123:
    #     failure during conversion to COFF", identically on x64/ARM64/
    #     ARM64EC (the manifest pipeline is arch-independent). /MANIFEST:NO
    #     below removes the second embedder; the .res copy is the survivor.
    #   * MinGW/windres NEVER auto-embeds a manifest, so it needs the .rc
    #     copy — which is why cross builds passed while MSVC CI failed.
    # Regression guard: listing a .manifest file as a target source makes
    # MSBuild treat it as an Additional Manifest File and mt.exe would
    # embed it a second time — reintroducing CVT1100/LNK1123. Fail the
    # configure step loudly instead of shipping that time bomb.
    get_target_property(_kieekey_openkey_srcs openkey SOURCES)
    foreach(_kieekey_src IN LISTS _kieekey_openkey_srcs)
        if(_kieekey_src MATCHES "\\.manifest$")
            message(FATAL_ERROR
                "openkey: '${_kieekey_src}' must NOT be a target source. "
                "The manifest is embedded exactly once via KieeKeyApp.rc "
                "(RT_MANIFEST ID 1); a .manifest source makes MSBuild embed "
                "a second copy -> CVT1100 duplicate MANIFEST / LNK1123. "
                "Edit KieeKeyApp.manifest itself instead.")
        endif()
    endforeach()
    target_include_directories(openkey PRIVATE src/core src/tsf src/app)
    target_compile_features(openkey PRIVATE cxx_std_23)
    target_link_libraries(openkey PRIVATE ok_core ok_tsf shell32 comctl32 ole32)
    # UNICODE on EVERY toolchain: without it the MinGW headers resolve
    # IDI_APPLICATION/IDC_ARROW to the ANSI MAKEINTRESOURCEA variants and the
    # LoadIconW/LoadCursorW calls fail to compile. Both branches are now
    # explicit and identical.
    target_compile_definitions(openkey PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        # /MANIFEST:NO (see the INVARIANT comment above): the manifest comes
        # ONLY from KieeKeyApp.rc via the RC rule. Without this, MSVC's
        # linker auto-embeds its own RT_MANIFEST ID 1 alongside the .res
        # copy -> CVT1100 duplicate resource (MANIFEST, 1, 0x0409) ->
        # LNK1123, on every VS-generator arch (x64/ARM64/ARM64EC).
        target_link_options(openkey PRIVATE /SUBSYSTEM:WINDOWS /entry:wWinMainCRTStartup /MANIFEST:NO)
    else()
        # MinGW: -mwindows (GUI subsystem, no console) + -municode (wWinMain)
        target_link_options(openkey PRIVATE -mwindows -municode)
    endif()

else()

    #------------------------------------------------------------------------
    # Non-Windows host (Linux/macOS CI): the Windows-only TUs cannot build
    # here, so verify the platform-independent logic through the shim:
    #   - ok_wrap_tests : InlineEmitter batching + HookWatchdog state machine
    #                     + Vyukov OutputRing (OK_WRAP_NO_WIN32 + win32_shim)
    #   - ok_tests      : TextEngine golden vectors + lock-free ring stress
    #------------------------------------------------------------------------
    if(KIEEKEY_BUILD_TESTS)
        enable_testing()

        add_executable(ok_wrap_tests
            tests/test_win32wrapper.cpp
            src/core/win32_wrapper.cpp
        )
        target_compile_definitions(ok_wrap_tests PRIVATE OK_WRAP_NO_WIN32)
        target_include_directories(ok_wrap_tests PRIVATE src/core tests)
        target_compile_features(ok_wrap_tests PRIVATE cxx_std_23)
        add_test(NAME ok_wrap_tests COMMAND ok_wrap_tests)

        add_executable(ok_tests
            tests/test_textengine.cpp
            src/core/TextEngine.cpp
        )
        target_include_directories(ok_tests PRIVATE src/core)
        target_compile_features(ok_tests PRIVATE cxx_std_23)
        add_test(NAME ok_tests COMMAND ok_tests)

        add_executable(ok_ring_tests tests/test_ringbuffer.cpp)
        target_include_directories(ok_ring_tests PRIVATE src/core)
        target_compile_features(ok_ring_tests PRIVATE cxx_std_23)
        add_test(NAME ok_ring_tests COMMAND ok_ring_tests)

        # Hard per-test caps: a future regression that turns a barrier/wait
        # path into an infinite block must fail CI in minutes, not hang the
        # job until GitHub's hour-level limit kills it.
        set_tests_properties(ok_tests ok_ring_tests ok_wrap_tests
                             PROPERTIES TIMEOUT 120)

        #-------------------------------------------------------------------
        # v1.2.0 Stable — the FULL suite is now registered with CTest.
        #
        # Before this release only ok_tests / ok_ring_tests / ok_wrap_tests
        # were ctest targets: twelve more harnesses (stress, soak,
        # differential, hotfix, output-item, v3.3.1 features) lived in tests/
        # and were run by hand, if at all. A test that is never executed
        # cannot protect a release — the release gate now runs all of them,
        # with a hard timeout each so a future hang fails the job in minutes
        # instead of burning the CI budget.
        #-------------------------------------------------------------------
        # Portable harnesses (pure std + the shim; identical on every host).
        add_executable(ok_outputitem_tests tests/test_outputitem.cpp src/core/TextEngine.cpp)
        target_include_directories(ok_outputitem_tests PRIVATE src/core)
        target_compile_features(ok_outputitem_tests PRIVATE cxx_std_23)

        add_executable(ok_hotfix_tests tests/test_hotfix.cpp src/core/TextEngine.cpp)
        target_include_directories(ok_hotfix_tests PRIVATE src/core)
        target_compile_features(ok_hotfix_tests PRIVATE cxx_std_23)

        # v1.2.1 RC2 — live engine vs frozen v1.2.1 RC1 engine, lockstep
        # differential over randomized keystreams × the option matrix.
        add_library(ok_engine_rc1_ref OBJECT tests/reference/kieekey-1.2.1-rc1/TextEngine.cpp)
        target_include_directories(ok_engine_rc1_ref PRIVATE tests/reference/kieekey-1.2.1-rc1)
        target_compile_definitions(ok_engine_rc1_ref PRIVATE ok=ok_rc1)
        target_compile_features(ok_engine_rc1_ref PRIVATE cxx_std_23)
        add_executable(ok_diff_ab_tests tests/diff_engine_ab.cpp src/core/TextEngine.cpp
                       $<TARGET_OBJECTS:ok_engine_rc1_ref>)
        target_include_directories(ok_diff_ab_tests PRIVATE src/core tests)
        target_compile_features(ok_diff_ab_tests PRIVATE cxx_std_23)

        # v1.2.1 RC2 — notification framework + QuickTelex detector + profiles.
        add_executable(ok_notify_tests tests/test_notifications.cpp src/core/TextEngine.cpp)
        target_include_directories(ok_notify_tests PRIVATE src/core)
        target_compile_features(ok_notify_tests PRIVATE cxx_std_23)
        add_executable(ok_stress_rc2 tests/stress_rc2.cpp src/core/TextEngine.cpp)
        target_include_directories(ok_stress_rc2 PRIVATE src/core)
        target_compile_features(ok_stress_rc2 PRIVATE cxx_std_23)
        find_package(Threads REQUIRED)
        target_link_libraries(ok_stress_rc2 PRIVATE Threads::Threads)

        add_executable(ok_v331_tests tests/test_v331_features.cpp src/core/TextEngine.cpp)
        target_include_directories(ok_v331_tests PRIVATE src/core)
        target_compile_features(ok_v331_tests PRIVATE cxx_std_23)

        add_executable(ok_stress_tests tests/stress_engine.cpp src/core/TextEngine.cpp)
        target_include_directories(ok_stress_tests PRIVATE src/core)
        target_compile_features(ok_stress_tests PRIVATE cxx_std_23)

        add_executable(ok_soak_tests tests/soak_engine.cpp src/core/TextEngine.cpp)
        target_include_directories(ok_soak_tests PRIVATE src/core)
        target_compile_features(ok_soak_tests PRIVATE cxx_std_23)

        add_executable(ok_queue_stress_tests tests/stress_queue.cpp src/core/TextEngine.cpp)
        target_include_directories(ok_queue_stress_tests PRIVATE src/core)
        target_compile_features(ok_queue_stress_tests PRIVATE cxx_std_23)

        add_executable(ok_gate_tests
            tests/gate_correctness.cpp src/core/TextEngine.cpp)
        target_compile_definitions(ok_gate_tests PRIVATE NO_205)
        target_include_directories(ok_gate_tests PRIVATE src/core tests imebench_kit/harness)
        target_compile_features(ok_gate_tests PRIVATE cxx_std_17)

        # v1.2.0 Stable — new suites (keystroke correctness, state-transition
        # matrix, pipeline lifecycle + wake protocol, transport soak).
        add_executable(ok_key_tests
            tests/test_key_correctness.cpp src/core/TextEngine.cpp)
        target_include_directories(ok_key_tests PRIVATE src/core)
        target_compile_features(ok_key_tests PRIVATE cxx_std_23)

        add_executable(ok_state_tests
            tests/test_state_transitions.cpp src/core/TextEngine.cpp)
        target_include_directories(ok_state_tests PRIVATE src/core)
        target_compile_features(ok_state_tests PRIVATE cxx_std_23)

        add_executable(ok_lifecycle_tests
            tests/test_lifecycle.cpp src/core/TextEngine.cpp)
        target_include_directories(ok_lifecycle_tests PRIVATE src/core tests)
        target_compile_features(ok_lifecycle_tests PRIVATE cxx_std_23)
        find_package(Threads REQUIRED)
        target_link_libraries(ok_lifecycle_tests PRIVATE Threads::Threads)

        add_executable(ok_soak_pipeline tests/soak_pipeline.cpp)
        target_include_directories(ok_soak_pipeline PRIVATE src/core tests)
        target_compile_features(ok_soak_pipeline PRIVATE cxx_std_23)
        target_link_libraries(ok_soak_pipeline PRIVATE Threads::Threads)

        # v1.2.2 RC2 — performance-preference strategy model (pure header,
        # every field pinned incl. the adaptive telemetry ladder and the ONE
        # engine-visible mapping) + the full option-matrix harness (30 method
        # x table x encoding configs, pairwise-complete grammar-boolean cover,
        # live/contract option-switch matrices, VIQR channel contract) locked
        # step against the reference oracle.
        add_executable(ok_perf_profiles tests/test_perf_profiles.cpp)
        target_include_directories(ok_perf_profiles PRIVATE src/core)
        target_compile_features(ok_perf_profiles PRIVATE cxx_std_23)

        add_executable(ok_option_matrix
            tests/test_option_matrix.cpp src/core/TextEngine.cpp)
        target_include_directories(ok_option_matrix PRIVATE src/core tests)
        target_compile_features(ok_option_matrix PRIVATE cxx_std_23)
        target_link_libraries(ok_option_matrix PRIVATE Threads::Threads)

        # Shared portable feature implementation. Keep graphical/server tests
        # registered with CTest too (the shell runner is not the only build).
        add_library(ok_arcade_portable STATIC
            src/core/Arcade.cpp src/core/ArcadeFrame.cpp src/core/ArcadeRender.cpp
            src/core/Progression.cpp src/core/ArcadeServer.cpp
            src/core/ChaosEngine.cpp src/core/AiRival.cpp src/core/TypingAnalytics.cpp)
        target_include_directories(ok_arcade_portable PUBLIC src/core)
        target_link_libraries(ok_arcade_portable PUBLIC Threads::Threads)

        add_executable(ok_arcade_tests tests/test_arcade.cpp)
        target_link_libraries(ok_arcade_tests PRIVATE ok_arcade_portable)
        add_executable(ok_arcade_render_tests tests/test_arcade_render.cpp)
        target_link_libraries(ok_arcade_render_tests PRIVATE ok_arcade_portable)
        add_executable(ok_arcade_server_tests tests/test_arcade_server.cpp)
        target_link_libraries(ok_arcade_server_tests PRIVATE ok_arcade_portable)
        add_executable(ok_arcade_window_tests
            tests/test_arcade_window.cpp tests/win32_gdi_stub.cpp
            src/app/ArcadeWindow.cpp src/app/ChaosLabWindow.cpp)
        target_include_directories(ok_arcade_window_tests PRIVATE
            tests tests/win32_headers src/app src/core)
        target_compile_definitions(ok_arcade_window_tests PRIVATE _WIN32)
        target_compile_options(ok_arcade_window_tests PRIVATE
            "-include${CMAKE_SOURCE_DIR}/tests/win32_gdi_shim.hpp")
        target_link_libraries(ok_arcade_window_tests PRIVATE ok_arcade_portable)
        kieekey_test_force_asserts(ok_arcade_window_tests)

        add_executable(ok_chaos_tests tests/test_chaos.cpp src/core/ChaosEngine.cpp)
        target_include_directories(ok_chaos_tests PRIVATE src/core)
        target_compile_features(ok_chaos_tests PRIVATE cxx_std_23)

        add_executable(ok_ai_tests tests/test_ai_rival.cpp src/core/AiRival.cpp)
        target_include_directories(ok_ai_tests PRIVATE src/core)
        target_compile_features(ok_ai_tests PRIVATE cxx_std_23)

        add_executable(ok_progression_tests tests/test_progression.cpp src/core/Progression.cpp)
        target_include_directories(ok_progression_tests PRIVATE src/core)
        target_compile_features(ok_progression_tests PRIVATE cxx_std_23)

        add_executable(ok_analytics_tests tests/test_analytics.cpp src/core/TypingAnalytics.cpp)
        target_include_directories(ok_analytics_tests PRIVATE src/core)
        target_compile_features(ok_analytics_tests PRIVATE cxx_std_23)

        add_executable(ok_online_tests tests/test_online_ghost.cpp src/core/OnlineGhost.cpp)
        target_include_directories(ok_online_tests PRIVATE src/core)
        target_compile_features(ok_online_tests PRIVATE cxx_std_23)

        add_executable(ok_soak_arcade tests/test_soak_arcade.cpp)
        target_link_libraries(ok_soak_arcade PRIVATE ok_arcade_portable)
        target_include_directories(ok_soak_arcade PRIVATE src/core)
        target_compile_features(ok_soak_arcade PRIVATE cxx_std_23)

        add_test(NAME ok_outputitem_tests COMMAND ok_outputitem_tests)
        add_test(NAME ok_hotfix_tests    COMMAND ok_hotfix_tests)
        add_test(NAME ok_diff_ab_tests   COMMAND ok_diff_ab_tests)
        add_test(NAME ok_notify_tests    COMMAND ok_notify_tests)
        add_test(NAME ok_stress_rc2      COMMAND ok_stress_rc2 500000)
        add_test(NAME ok_v331_tests      COMMAND ok_v331_tests)
        # stress: <seed> <keyCount> — 1M fuzzed keys, invariants + determinism
        add_test(NAME ok_stress_tests    COMMAND ok_stress_tests 1 1000000)
        # soak: <iterations> <checkpointEvery> — flat-allocation/RSS check
        add_test(NAME ok_soak_tests      COMMAND ok_soak_tests 200000 20000)
        add_test(NAME ok_queue_stress_tests COMMAND ok_queue_stress_tests)
        add_test(NAME ok_gate_tests      COMMAND ok_gate_tests)
        add_test(NAME ok_key_tests       COMMAND ok_key_tests)
        add_test(NAME ok_state_tests     COMMAND ok_state_tests)
        add_test(NAME ok_lifecycle_tests COMMAND ok_lifecycle_tests)
        add_test(NAME ok_soak_pipeline   COMMAND ok_soak_pipeline 300000 30000)
        add_test(NAME ok_perf_profiles   COMMAND ok_perf_profiles)
        # default depth = 20k random events/config; deeper runs are manual:
        #   ctest -R ok_option_matrix --extra-args ... or run the exe directly
        add_test(NAME ok_option_matrix   COMMAND ok_option_matrix)

        # v1.3.0 Arcade, Chaos, AI, Progression, Analytics & Ghost
        add_test(NAME ok_arcade_tests      COMMAND ok_arcade_tests)
        add_test(NAME ok_arcade_render_tests COMMAND ok_arcade_render_tests)
        add_test(NAME ok_arcade_server_tests COMMAND ok_arcade_server_tests)
        add_test(NAME ok_arcade_window_tests COMMAND ok_arcade_window_tests)
        set_tests_properties(ok_arcade_window_tests ok_arcade_render_tests ok_arcade_server_tests
                             PROPERTIES TIMEOUT 120)
        add_test(NAME ok_chaos_tests       COMMAND ok_chaos_tests)
        add_test(NAME ok_ai_tests          COMMAND ok_ai_tests)
        add_test(NAME ok_progression_tests COMMAND ok_progression_tests)
        add_test(NAME ok_analytics_tests   COMMAND ok_analytics_tests)
        add_test(NAME ok_online_tests      COMMAND ok_online_tests)
        add_test(NAME ok_soak_arcade       COMMAND ok_soak_arcade 500)

        # Live asserts in every configuration for every harness above (see
        # kieekey_test_force_asserts). The frozen RC1 reference object lib
        # and the manual bench executables stay NDEBUG on purpose.
        foreach(_k_test_tgt IN ITEMS
                ok_wrap_tests ok_tests ok_ring_tests
                ok_outputitem_tests ok_hotfix_tests ok_diff_ab_tests
                ok_notify_tests ok_stress_rc2 ok_v331_tests
                ok_stress_tests ok_soak_tests ok_queue_stress_tests
                ok_gate_tests ok_key_tests ok_state_tests ok_lifecycle_tests
                ok_soak_pipeline ok_perf_profiles ok_option_matrix
                ok_arcade_tests ok_arcade_render_tests ok_arcade_server_tests
                ok_chaos_tests ok_ai_tests
                ok_progression_tests ok_analytics_tests ok_online_tests
                ok_soak_arcade)
            kieekey_test_force_asserts(${_k_test_tgt})
        endforeach()
        if(TARGET ok_arcade_window_tests)
            kieekey_test_force_asserts(ok_arcade_window_tests)
        endif()

        # Every registered test gets a hard cap. A regression that turns a
        # barrier/wait path into an infinite block must fail the gate in
        # minutes, not hang the job until the runner's own limit kills it.
        set_tests_properties(
            ok_tests ok_ring_tests ok_wrap_tests
            ok_outputitem_tests ok_hotfix_tests ok_diff_ab_tests ok_notify_tests ok_stress_rc2 ok_v331_tests
            ok_stress_tests ok_soak_tests ok_queue_stress_tests
            ok_gate_tests ok_key_tests ok_state_tests ok_lifecycle_tests
            ok_soak_pipeline ok_perf_profiles
            ok_arcade_tests ok_chaos_tests ok_ai_tests ok_progression_tests
            ok_analytics_tests ok_online_tests ok_soak_arcade
            PROPERTIES TIMEOUT 300)
        set_tests_properties(ok_option_matrix PROPERTIES TIMEOUT 600)

        # Version-identifier consistency (release-gate requirement: no mixed
        # version strings). Skipped when no interpreter is available.
        find_package(Python3 COMPONENTS Interpreter QUIET)
        if(Python3_Interpreter_FOUND)
            add_test(NAME ok_input_isolation
                     COMMAND ${Python3_EXECUTABLE}
                             ${CMAKE_SOURCE_DIR}/scripts/check_input_isolation.py
                             --repo=${CMAKE_SOURCE_DIR})
            set_tests_properties(ok_input_isolation PROPERTIES TIMEOUT 60)
            add_test(NAME ok_version_check
                     COMMAND ${Python3_EXECUTABLE}
                             ${CMAKE_SOURCE_DIR}/scripts/check_version.py
                             --repo=${CMAKE_SOURCE_DIR})
            set_tests_properties(ok_version_check PROPERTIES TIMEOUT 60)
        endif()

        # E2E latency benchmark through the shim (run manually):
        #   ./ok_e2e_bench --keys=100000   → p50/p99/p99.9 + RSS report
        add_executable(ok_e2e_bench
            tests/e2e_bench.cpp
            src/core/win32_wrapper.cpp
            src/core/TextEngine.cpp
        )
        target_compile_definitions(ok_e2e_bench PRIVATE OK_WRAP_NO_WIN32)
        target_include_directories(ok_e2e_bench PRIVATE src/core tests)
        target_compile_features(ok_e2e_bench PRIVATE cxx_std_23)

        # tone-population latency benchmark (Phase 2) — per-population ×
        # output-path per-stage percentiles through the REAL engine + emitter
        # + ring + ordering barrier. Run manually:
        #   ./ok_tone_bench --pop=all --path=inline --commit-ns=0
        add_executable(ok_tone_bench
            tests/bench_tone_latency.cpp
            src/core/win32_wrapper.cpp
            src/core/TextEngine.cpp
        )
        target_compile_definitions(ok_tone_bench PRIVATE OK_WRAP_NO_WIN32)
        target_include_directories(ok_tone_bench PRIVATE src/core tests)
        target_compile_features(ok_tone_bench PRIVATE cxx_std_23)

        # v1.2.1 RC1 — real-world typing benchmark (human-like patterns,
        # reports latency AND resource cost). Run manually:
        #   ./ok_realworld_bench --keys=50000 --runs=3
        add_executable(ok_realworld_bench
            tests/bench_real_world_typing.cpp
            src/core/TextEngine.cpp
        )
        target_include_directories(ok_realworld_bench PRIVATE src/core)
        target_compile_features(ok_realworld_bench PRIVATE cxx_std_20)

        # v1.2.1 RC2 — Performance Preference Profiles through the shim
        # pipeline. Run manually:  ./ok_profiles_bench --keys=100000 --runs=3
        add_executable(ok_profiles_bench
            tests/bench_profiles.cpp
            src/core/win32_wrapper.cpp
            src/core/TextEngine.cpp
        )
        target_include_directories(ok_profiles_bench PRIVATE src/core tests)
        target_compile_definitions(ok_profiles_bench PRIVATE OK_WRAP_NO_WIN32)
        target_compile_features(ok_profiles_bench PRIVATE cxx_std_20)
        target_link_libraries(ok_profiles_bench PRIVATE Threads::Threads)

        # v1.3.0 — Subsystem Isolation & Apples-to-Apples benchmark:
        add_executable(ok_iso_bench
            tests/bench_v130_isolation.cpp
            src/core/TextEngine.cpp
        )
        target_link_libraries(ok_iso_bench PRIVATE ok_arcade_portable)
        target_include_directories(ok_iso_bench PRIVATE src/core tests)
        target_compile_features(ok_iso_bench PRIVATE cxx_std_23)

        message(STATUS "Non-Windows host: building the shim-based test suite and "
                       "benchmarks (including Arcade, Lab UI and source-contract gates). "
                       "The Windows product targets (ok_core, ok_tsf, KieeKeyApp.exe) "
                       "require WIN32.")
    endif()

endif()

#----------------------------------------------------------------------------
# Windows-only test harness (golden vectors, queue stress, wrapper logic,
# E2E latency benchmark) — links the real production library.
#----------------------------------------------------------------------------
if(WIN32 AND KIEEKEY_BUILD_TESTS)
    enable_testing()

    add_executable(ok_tests
        tests/test_textengine.cpp
    )
    target_link_libraries(ok_tests PRIVATE ok_core)
    add_test(NAME ok_tests COMMAND ok_tests)

    add_executable(ok_ring_tests tests/test_ringbuffer.cpp)
    target_link_libraries(ok_ring_tests PRIVATE ok_core)
    add_test(NAME ok_ring_tests COMMAND ok_ring_tests)

    # v1.2.2 RC2 — the two new harnesses live HERE (with the always-built
    # basics), NOT in the KIEEKEY_BUILD_EXTRA_TESTS block: that block is
    # OFF-by-default on Windows because its legacy files are unvetted under
    # /W4 /WX (see comment above the extras section). These two files are
    # their evidence gates (option-matrix coherence + strategy-model
    # profiles) are release-critical, so they are built and run on every
    # Windows CI leg. std::thread needs no extra link under MSVC; no Threads
    # package is consulted in this block.
    add_executable(ok_perf_profiles tests/test_perf_profiles.cpp)
    target_include_directories(ok_perf_profiles PRIVATE src/core)
    target_link_libraries(ok_perf_profiles PRIVATE ok_core)
    add_executable(ok_option_matrix tests/test_option_matrix.cpp)
    target_include_directories(ok_option_matrix PRIVATE src/core tests)
    target_link_libraries(ok_option_matrix PRIVATE ok_core)
    if(MSVC)
        # Harness-scoped /wd, not source edits: these two TUs legitimately
        # hammer size_t<->uint32 boundaries (event counts, stream offsets,
        # RNG folds — C4244/C4267/C4310), use constant conditions and shadowed
        # loop indices freely in test scaffolding (C4100/C4189/C4456-9), and
        # call [[nodiscard]] engine members while driving state, discarding
        # the result on purpose (C4834 — the exact class the extras-off
        # rationale names). The g++ gates (-Wall -Wextra, and the suite's
        # -Wpedantic run) stay warning-clean, so nothing hides there.
        foreach(_okrc2_tgt IN ITEMS ok_perf_profiles ok_option_matrix)
            target_compile_options(${_okrc2_tgt} PRIVATE
                /wd4244 /wd4267 /wd4310 /wd4100 /wd4189
                /wd4456 /wd4457 /wd4458 /wd4459
                /wd4701 /wd4702 /wd4505 /wd4834)
        endforeach()
    endif()
    add_test(NAME ok_perf_profiles COMMAND ok_perf_profiles)
    add_test(NAME ok_option_matrix COMMAND ok_option_matrix)
    # CI depth is the harness default (20k events/config, ~60-120 s on MSVC);
    # deep campaign runs pass --events explicitly.
    set_tests_properties(ok_option_matrix PROPERTIES TIMEOUT 600)

    # wrapper logic tests (batched emitter, watchdog state machine,
    # Vyukov Ring 1024). The assertions inspect the RECORDED SendInput calls
    # and a controlled fake clock — that only exists in the deterministic
    # shim (tests/win32_shim.hpp), so this target goes through the shim on
    # EVERY platform (identical configuration to the non-Windows branch).
    add_executable(ok_wrap_tests
        tests/test_win32wrapper.cpp
        src/core/win32_wrapper.cpp
    )
    target_compile_definitions(ok_wrap_tests PRIVATE OK_WRAP_NO_WIN32)
    target_include_directories(ok_wrap_tests PRIVATE src/core tests)
    add_test(NAME ok_wrap_tests COMMAND ok_wrap_tests)

    # Live asserts in every configuration (see kieekey_test_force_asserts)
    # for the harnesses the x64 CI leg actually runs.
    foreach(_k_test_tgt IN ITEMS
            ok_tests ok_ring_tests ok_perf_profiles ok_option_matrix
            ok_wrap_tests)
        kieekey_test_force_asserts(${_k_test_tgt})
    endforeach()

    #-----------------------------------------------------------------------
    # v1.2.0 Stable — EXTENDED suites, OPT-IN on Windows.
    #
    # These eleven harnesses are the same ones the Linux job runs from
    # tests/run_all_tests.sh. They are OFF by default on Windows, and that is
    # deliberate, not an oversight:
    #
    #   * several of them were written against POSIX (gate_correctness.cpp
    #     installs a SIGBUS handler via sigjmp_buf; soak_engine.cpp reads
    #     /proc/self/status) and simply do not compile against the Windows
    #     SDK;
    #   * the Windows CI job compiles with /W4 /WX — warnings as errors —
    #     and none of these files has ever been built by MSVC, so any level-4
    #     diagnostic (C4244 narrowing, C4267 size_t truncation, C4477 format,
    #     C4834 nodiscard, ...) becomes a hard build failure on ALL THREE CI
    #     platforms, because `cmake --build` builds every defined target.
    #
    # Restoring the CI contract the workflow file documents ("ctest: ok_tests,
    # ok_ring_tests, ok_wrap_tests") is worth more than building unvetted
    # harnesses. Enable locally to port them:
    #     cmake -S . -B out -DKIEEKEY_BUILD_EXTRA_TESTS=ON
    #-----------------------------------------------------------------------
    if(KIEEKEY_BUILD_EXTRA_TESTS)
    add_executable(ok_outputitem_tests tests/test_outputitem.cpp)
    target_link_libraries(ok_outputitem_tests PRIVATE ok_core)
    add_executable(ok_hotfix_tests tests/test_hotfix.cpp)
    target_link_libraries(ok_hotfix_tests PRIVATE ok_core)
    add_library(ok_engine_rc1_ref OBJECT tests/reference/kieekey-1.2.1-rc1/TextEngine.cpp)
    target_include_directories(ok_engine_rc1_ref PRIVATE tests/reference/kieekey-1.2.1-rc1)
    target_compile_definitions(ok_engine_rc1_ref PRIVATE ok=ok_rc1)
    target_compile_features(ok_engine_rc1_ref PRIVATE cxx_std_23)
    add_executable(ok_diff_ab_tests tests/diff_engine_ab.cpp $<TARGET_OBJECTS:ok_engine_rc1_ref>)
    target_include_directories(ok_diff_ab_tests PRIVATE tests)
    target_link_libraries(ok_diff_ab_tests PRIVATE ok_core)
    add_executable(ok_notify_tests tests/test_notifications.cpp)
    target_link_libraries(ok_notify_tests PRIVATE ok_core)
    add_executable(ok_stress_rc2 tests/stress_rc2.cpp)
    target_link_libraries(ok_stress_rc2 PRIVATE ok_core)
    add_executable(ok_v331_tests tests/test_v331_features.cpp)
    target_link_libraries(ok_v331_tests PRIVATE ok_core)
    add_executable(ok_stress_tests tests/stress_engine.cpp)
    target_link_libraries(ok_stress_tests PRIVATE ok_core)
    add_executable(ok_soak_tests tests/soak_engine.cpp)
    target_link_libraries(ok_soak_tests PRIVATE ok_core)
    add_executable(ok_queue_stress_tests tests/stress_queue.cpp)
    target_link_libraries(ok_queue_stress_tests PRIVATE ok_core)
    add_executable(ok_gate_tests tests/gate_correctness.cpp)
    target_compile_definitions(ok_gate_tests PRIVATE NO_205)
    target_include_directories(ok_gate_tests PRIVATE tests imebench_kit/harness)
    target_link_libraries(ok_gate_tests PRIVATE ok_core)

    add_executable(ok_key_tests tests/test_key_correctness.cpp)
    target_link_libraries(ok_key_tests PRIVATE ok_core)

    add_executable(ok_state_tests tests/test_state_transitions.cpp)
    target_link_libraries(ok_state_tests PRIVATE ok_core)
    add_executable(ok_lifecycle_tests tests/test_lifecycle.cpp)
    target_link_libraries(ok_lifecycle_tests PRIVATE ok_core)
    add_executable(ok_soak_pipeline tests/soak_pipeline.cpp)
    target_link_libraries(ok_soak_pipeline PRIVATE ok_core)

    # v1.3.0 Arcade, Chaos, AI & Progression harnesses
    add_executable(ok_arcade_tests tests/test_arcade.cpp)
    target_link_libraries(ok_arcade_tests PRIVATE ok_core)
    add_executable(ok_arcade_render_tests tests/test_arcade_render.cpp)
    target_link_libraries(ok_arcade_render_tests PRIVATE ok_core)
    add_executable(ok_arcade_server_tests tests/test_arcade_server.cpp)
    target_link_libraries(ok_arcade_server_tests PRIVATE ok_core)

    add_executable(ok_chaos_tests tests/test_chaos.cpp)
    target_link_libraries(ok_chaos_tests PRIVATE ok_core)
    add_executable(ok_ai_tests tests/test_ai_rival.cpp)
    target_link_libraries(ok_ai_tests PRIVATE ok_core)
    add_executable(ok_progression_tests tests/test_progression.cpp)
    target_link_libraries(ok_progression_tests PRIVATE ok_core)
    add_executable(ok_analytics_tests tests/test_analytics.cpp)
    target_link_libraries(ok_analytics_tests PRIVATE ok_core)
    add_executable(ok_online_tests tests/test_online_ghost.cpp)
    target_link_libraries(ok_online_tests PRIVATE ok_core)
    add_executable(ok_soak_arcade tests/test_soak_arcade.cpp)
    target_link_libraries(ok_soak_arcade PRIVATE ok_core)

    add_test(NAME ok_outputitem_tests COMMAND ok_outputitem_tests)
    add_test(NAME ok_hotfix_tests    COMMAND ok_hotfix_tests)
    add_test(NAME ok_diff_ab_tests   COMMAND ok_diff_ab_tests)
    add_test(NAME ok_notify_tests    COMMAND ok_notify_tests)
    add_test(NAME ok_stress_rc2      COMMAND ok_stress_rc2 500000)
    add_test(NAME ok_v331_tests      COMMAND ok_v331_tests)
    add_test(NAME ok_stress_tests    COMMAND ok_stress_tests 1 1000000)
    add_test(NAME ok_soak_tests      COMMAND ok_soak_tests 200000 20000)
    add_test(NAME ok_queue_stress_tests COMMAND ok_queue_stress_tests)
    add_test(NAME ok_gate_tests      COMMAND ok_gate_tests)
    add_test(NAME ok_key_tests       COMMAND ok_key_tests)
    add_test(NAME ok_state_tests     COMMAND ok_state_tests)
    add_test(NAME ok_lifecycle_tests COMMAND ok_lifecycle_tests)
    add_test(NAME ok_soak_pipeline   COMMAND ok_soak_pipeline 300000 30000)
    add_test(NAME ok_arcade_tests      COMMAND ok_arcade_tests)
    add_test(NAME ok_chaos_tests       COMMAND ok_chaos_tests)
    add_test(NAME ok_ai_tests          COMMAND ok_ai_tests)
    add_test(NAME ok_progression_tests COMMAND ok_progression_tests)
    add_test(NAME ok_analytics_tests   COMMAND ok_analytics_tests)
    add_test(NAME ok_online_tests      COMMAND ok_online_tests)
    add_test(NAME ok_soak_arcade       COMMAND ok_soak_arcade 500)

    # Same live-asserts rule for the opt-in extended harnesses (see
    # kieekey_test_force_asserts); ok_engine_rc1_ref stays frozen.
    foreach(_k_test_tgt IN ITEMS
            ok_outputitem_tests ok_hotfix_tests ok_diff_ab_tests
            ok_notify_tests ok_stress_rc2 ok_v331_tests
            ok_stress_tests ok_soak_tests ok_queue_stress_tests
            ok_gate_tests ok_key_tests ok_state_tests ok_lifecycle_tests
            ok_soak_pipeline
            ok_arcade_tests ok_chaos_tests ok_ai_tests
            ok_progression_tests ok_analytics_tests ok_online_tests
            ok_soak_arcade)
        kieekey_test_force_asserts(${_k_test_tgt})
    endforeach()
    endif()   # KIEEKEY_BUILD_EXTRA_TESTS

    # Same hang protection for the Windows-compiled test suite (these run
    # on the native x64 CI job — the barrier/wait paths live here).
    set_tests_properties(
        ok_tests ok_ring_tests ok_wrap_tests
        PROPERTIES TIMEOUT 300)
    if(KIEEKEY_BUILD_EXTRA_TESTS)
        set_tests_properties(
            ok_outputitem_tests ok_hotfix_tests ok_diff_ab_tests ok_notify_tests ok_stress_rc2 ok_v331_tests
            ok_stress_tests ok_soak_tests ok_queue_stress_tests
            ok_gate_tests ok_key_tests ok_state_tests ok_lifecycle_tests
            ok_soak_pipeline
            ok_arcade_tests ok_chaos_tests ok_ai_tests ok_progression_tests
            ok_analytics_tests ok_online_tests ok_soak_arcade
            PROPERTIES TIMEOUT 300)
    endif()

    # Version-identifier consistency guard (MSVC builds must not ship mixed
    # version strings either).
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME ok_input_isolation
                 COMMAND ${Python3_EXECUTABLE}
                         ${CMAKE_SOURCE_DIR}/scripts/check_input_isolation.py
                         --repo=${CMAKE_SOURCE_DIR})
        set_tests_properties(ok_input_isolation PROPERTIES TIMEOUT 60)
        add_test(NAME ok_version_check
                 COMMAND ${Python3_EXECUTABLE}
                         ${CMAKE_SOURCE_DIR}/scripts/check_version.py
                         --repo=${CMAKE_SOURCE_DIR})
        set_tests_properties(ok_version_check PROPERTIES TIMEOUT 60)
    endif()

    # Performance / E2E benchmarks (not part of ctest — run manually).
    add_executable(ok_bench tests/bench_engine.cpp)
    target_link_libraries(ok_bench PRIVATE ok_core)
    add_executable(ok_e2e_bench tests/e2e_bench.cpp)
    target_link_libraries(ok_e2e_bench PRIVATE ok_core)
    # tone-population bench — REAL Win32 build (real SendInput, QPC,
    # EditDrainBarrier event flavor), same structure as e2e_bench.
    add_executable(ok_tone_bench tests/bench_tone_latency.cpp)
    target_link_libraries(ok_tone_bench PRIVATE ok_core)
    target_include_directories(ok_tone_bench PRIVATE src/core tests)

    # v1.3.0 Subsystem isolation benchmark
    add_executable(ok_iso_bench tests/bench_v130_isolation.cpp)
    target_link_libraries(ok_iso_bench PRIVATE ok_core)
    target_include_directories(ok_iso_bench PRIVATE src/core tests)
endif()

#---------------------------------------------------------------------------
# global profiler gate — when KIEEKEY_PROFILE=1 the QPC stage stamps
# compile into EVERY target (app + benches). Default 0 = zero profiler code.
#---------------------------------------------------------------------------
add_compile_definitions(KIEEKEY_PROFILE=${KIEEKEY_PROFILE})

# The profile switch is intentionally GLOBAL (not target-scoped): it changes
# EngineOptions' default member initializers and the constexpr profile flags
# in TextEngine.hpp, so every TU in the build must observe the same value —
# a target-scoped definition would let TUs disagree (ODR violation with
# silently mixed strict/profile behavior).
if(KIEEKEY_BUILD_TESTS)
    find_package(Threads REQUIRED)
    add_executable(ok_live_effects_tests tests/test_live_effects.cpp
        src/core/TextEngine.cpp src/core/ChaosEngine.cpp)
    target_include_directories(ok_live_effects_tests PRIVATE src/core)
    target_link_libraries(ok_live_effects_tests PRIVATE Threads::Threads)
    kieekey_test_force_asserts(ok_live_effects_tests)
    add_test(NAME ok_live_effects_tests COMMAND ok_live_effects_tests)
    set_tests_properties(ok_live_effects_tests PROPERTIES TIMEOUT 120)
endif()

if(KIEEKEY_LOW_LATENCY_PROFILE)
    add_compile_definitions(KIEEKEY_LOW_LATENCY_PROFILE)
endif()

#----------------------------------------------------------------------------
# WinUI 3 front-end (Windows App SDK; optional — requires the SDK installed)
#----------------------------------------------------------------------------
if(WIN32 AND KIEEKEY_BUILD_UI STREQUAL "winui3")
    # --- Windows App SDK via FetchContent/nuget (or point to an installed SDK)
    find_package(Microsoft.WindowsAppSDK 1.5 CONFIG REQUIRED)

    add_executable(kieekey_winui3
        src/ui/MainWindow.xaml
        src/ui/MainWindow.xaml.cpp
        src/ui/App.xaml
        src/ui/App.xaml.cpp
        src/ui/pch.h
        src/ui/pch.cpp
        # v1.3.0: the settings surface launches the native Arcade Hub / Chaos Lab
        # windows (ok::app::launchArcadeHub / launchChaosLab).
        src/app/ArcadeWindow.cpp
        src/app/ArcadeWindow.hpp
        src/app/ChaosLabWindow.cpp
        src/app/ChaosLabWindow.hpp
    )
    set_source_files_properties(src/ui/MainWindow.xaml PROPERTIES
        XAML_IMPORT "Microsoft.UI.Xaml.Markup.XamlReader")
    target_link_libraries(kieekey_winui3 PRIVATE ok_core ok_tsf)
    target_compile_features(kieekey_winui3 PRIVATE cxx_std_23)
endif()

#----------------------------------------------------------------------------
# Arcade Hub web bridge (tools/arcade_serve.cpp)
#
#   arcade_serve --port 8765 --host 0.0.0.0 --web web
#
# Serves web/ and drives the same ArcadeManager the GDI window paints, so the
# games are playable (and testable) on any machine, Windows or not.
#----------------------------------------------------------------------------
if(TARGET ok_core)
    # Standardized benchmark for the whole arcade pipeline (see
    # docs/bench/arcade-130/ARCADE_BENCH_REPORT.md and tests/run_arcade_bench.sh).
    add_executable(arcade_bench tools/arcade_bench.cpp)
    target_link_libraries(arcade_bench PRIVATE ok_core)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_package(Threads REQUIRED)
        target_link_libraries(arcade_bench PRIVATE Threads::Threads)
    endif()

    add_executable(arcade_serve tools/arcade_serve.cpp)
    target_link_libraries(arcade_serve PRIVATE ok_core)
    if(WIN32)
        target_link_libraries(arcade_serve PRIVATE ws2_32)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_package(Threads REQUIRED)
        target_link_libraries(arcade_serve PRIVATE Threads::Threads)
    endif()
    set_target_properties(arcade_serve PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    # The web assets ship next to the binary; the working directory of a ctest
    # run is the build tree, so copy them once at configure time.
    add_custom_target(arcade_web_assets ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_SOURCE_DIR}/web ${CMAKE_BINARY_DIR}/web
        COMMENT "Copying the Arcade Hub web client into the build tree")
endif()

#----------------------------------------------------------------------------
# Packaging: install the runnable app + headers
#----------------------------------------------------------------------------
if(WIN32)
    include(GNUInstallDirs)
    install(TARGETS openkey
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY src/core DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/openkey
            FILES_MATCHING PATTERN "*.hpp")
    # The web client of the Arcade Hub (tools/arcade_serve serves this folder;
    # KieeKeyApp.exe --arcade opens the native GDI window instead).
    install(DIRECTORY web DESTINATION ${CMAKE_INSTALL_DATADIR}/kieekey)
    # v1.3.0: the web bridge itself ships with the app, so the player can run
    # continuously on a machine the user controls (the hosted sandbox preview is
    # ephemeral by design) without a toolchain:
    #   arcade_serve --port 8765 --host 0.0.0.0 --web <install>/share/kieekey/web
    if(TARGET arcade_serve)
        install(TARGETS arcade_serve RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
        install(PROGRAMS scripts/run_web_bridge.sh DESTINATION ${CMAKE_INSTALL_DATADIR}/kieekey)
        install(FILES scripts/run_web_bridge.ps1 DESTINATION ${CMAKE_INSTALL_DATADIR}/kieekey)
    endif()
    if(TARGET arcade_bench)
        install(TARGETS arcade_bench RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
endif()
