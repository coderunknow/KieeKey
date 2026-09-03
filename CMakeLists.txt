#----------------------------------------------------------------------------
# KieeKey v1.1.3 — CMake build (modern replacement for the .sln/.vcxproj)
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
        VERSION 1.1.3
        DESCRIPTION "KieeKey — Vietnamese input method (modern core)"
        LANGUAGES CXX)

#----------------------------------------------------------------------------
# Options
#----------------------------------------------------------------------------
set(KIEEKEY_BUILD_UI "none" CACHE STRING "UI front-end: none | winui3")
set(KIEEKEY_BUILD_TESTS ON CACHE BOOL "Build the unit-test harness")
set(KIEEKEY_PROFILE 0 CACHE STRING "Compile in the QPC stage profiler (0=off/default, 1=instrumented)")
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
    add_compile_options(/W4 /WX /wd4324 /wd4127 /permissive- /Zc:__cplusplus /Zc:preprocessor /utf-8)
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
    )
    target_include_directories(ok_core PUBLIC src/core)
    target_compile_features(ok_core PUBLIC cxx_std_23)
    # Bare names — CMake maps them per toolchain (user32.lib / -luser32).
    target_link_libraries(ok_core PUBLIC
        user32 gdi32 shell32 ole32 dwmapi psapi version urlmon winmm
        advapi32)   # v1.1.3: OpenProcessToken / GetTokenInformation (elevation probe)

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

        message(STATUS "Non-Windows host: building the shim-based test suite only "
                       "(ok_tests, ok_ring_tests, ok_wrap_tests). The Windows product "
                       "targets (ok_core, ok_tsf, KieeKeyApp.exe) require WIN32.")
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

    # Same hang protection for the Windows-compiled test suite (these run
    # on the native x64 CI job — the barrier/wait paths live here).
    set_tests_properties(ok_tests ok_ring_tests ok_wrap_tests
                         PROPERTIES TIMEOUT 120)

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
endif()

#---------------------------------------------------------------------------
# global profiler gate — when KIEEKEY_PROFILE=1 the QPC stage stamps
# compile into EVERY target (app + benches). Default 0 = zero profiler code.
#---------------------------------------------------------------------------
add_compile_definitions(KIEEKEY_PROFILE=${KIEEKEY_PROFILE})

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
    )
    set_source_files_properties(src/ui/MainWindow.xaml PROPERTIES
        XAML_IMPORT "Microsoft.UI.Xaml.Markup.XamlReader")
    target_link_libraries(kieekey_winui3 PRIVATE ok_core ok_tsf)
    target_compile_features(kieekey_winui3 PRIVATE cxx_std_23)
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
endif()
