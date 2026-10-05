cmake_minimum_required(VERSION 3.16)
project(UrbanRecomp VERSION 1.2.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(SC_WORLD_LAYOUT "${CMAKE_CURRENT_SOURCE_DIR}/src/sc_world_guest_layout.h")
add_custom_command(OUTPUT "${SC_WORLD_LAYOUT}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/compile_world_layout.py"
    DEPENDS tools/compile_world_layout.py src/sc_world_guest.c
    VERBATIM)
set_source_files_properties(src/sc_world_guest.c PROPERTIES OBJECT_DEPENDS "${SC_WORLD_LAYOUT}")

# Vulkan shaders are compiled offline and embedded. Building and running the
# game needs no shader compiler, SDK or runtime shader files. Regenerate the
# checked-in SPIR-V with tools/compile_gpu_shaders.py after editing HLSL.

option(SC_CONSOLE "Open a Windows console for development diagnostics" OFF)
function(sc_configure_windows_app target)
    if(WIN32 AND NOT SC_CONSOLE)
        set_target_properties(${target} PROPERTIES WIN32_EXECUTABLE TRUE)
        if(MSVC)
            # SDL_MAIN_HANDLED leaves startup in our ordinary main(argc, argv).
            target_link_options(${target} PRIVATE /ENTRY:mainCRTStartup)
        endif()
    endif()
endfunction()

# Prerequisites: a snesrecomp checkout at ./snesrecomp (pinned submodule); the
# generated C for the variant you build (run `bash tools/regen.sh` with the
# verified ROM staged, see CONTRIBUTING.md); SDL3 (vcpkg: `sdl3:x64-windows`),
# or SDL2 with -DSNESRECOMP_SDL_BACKEND=SDL2.
set(SNESRECOMP_ROOT "${CMAKE_SOURCE_DIR}/snesrecomp" CACHE PATH
    "Path to the snesrecomp checkout used by this game")

include(${SNESRECOMP_ROOT}/runner/runner.cmake)

# The pre-boot launcher (settings, key bindings, the Sylt mod) comes from the
# shared recomp-ui submodule. Without the checkout the game builds and starts
# directly, with the settings in sc-settings.ini.
set(RECOMP_UI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/recomp-ui" CACHE PATH
    "Shared launcher (recomp-ui) checkout")
if(EXISTS "${RECOMP_UI_ROOT}/recomp_ui.cmake")
    set(RECOMP_UI_ENABLE_MODS ON CACHE BOOL "Urban Recomp launcher mods (Sylt)" FORCE)
    include(${RECOMP_UI_ROOT}/recomp_ui.cmake)
    set(SC_HAVE_LAUNCHER ON)
else()
    message(STATUS "UrbanRecomp: no recomp-ui checkout, building without "
                   "the launcher (git submodule update --init recomp-ui)")
    set(SC_HAVE_LAUNCHER OFF)
endif()

# The enhanced host uses program_gen's compatible native C ABI. The older
# src/gen AOT/CpuState ABI belongs to separate opt-in probe targets and must
# not bypass the enhanced host's per-edge game hooks and device timing.
if(EXISTS ${CMAKE_SOURCE_DIR}/src/gen)
    file(GLOB SC_GEN_PROBE ${CMAKE_SOURCE_DIR}/src/gen/*.c)
    if(SC_GEN_PROBE)
        message(STATUS
            "UrbanRecomp: src/gen has ${SC_GEN_PROBE} generated "
            "legacy AOT bank(s) for opt-in probes; the enhanced host uses "
            "program_gen native execution. See docs/NATIVE_EXECUTION.md.")
    endif()
endif()

add_executable(UrbanRecomp
    src/main.c
    src/sc_world.c
    src/sc_world_guest.c src/sc_postpass.c src/sc_sweep.c src/sc_power_traversal.c src/sc_zoning.c src/sc_smoothing.c src/sc_service.c src/sc_density.c src/sc_tile_lookup.c src/sc_land.c src/sc_transport.c src/sc_infrastructure.c
    src/sc_journey.c
    src/sc_development.c src/sc_development_batches.c
    src/sc_population.c
    src/sc_mouse_ui.c
    src/sc_construction.c src/sc_refresh_clock.c src/sc_power_refresh.c
    src/sc_clipboard.c
    src/sc_math.c
    src/sc_wait.c
    src/sc_sprite.c
    src/sc_music.c src/sc_pcm.c
    src/sc_launcher.c
    src/sc_sram.c
    src/sc_test_city.c
    src/sc_icon.c
    src/sc_mapview.c
    src/sc_vehicles.c
    src/sc_fleet.c
    src/sc_selector.c
    src/sc_titlesign.c
    src/sc_mapgen.c
        src/sc_decomp.c
    src/sc_video.c
    $<$<BOOL:${SC_HAVE_LAUNCHER}>:${CMAKE_SOURCE_DIR}/src/sc_mods.c>
    src/sc_renderer.c
    src/sc_terrain.c src/sc_native_ppu.c src/sc_obj.c src/sc_gpu_terrain.c src/sc_gpu_fields.c
)

target_include_directories(UrbanRecomp PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/recomp
    ${SNESRECOMP_RUNNER_INCLUDE_DIRS}
)

# Phase-1 needs only the SNES hardware/device models + the standalone
# interp816 core, not the AOT/CpuState runtime (common_rtl.c,
# common_cpu_infra.c, cpu_state.c, interp_bridge.c, debug_server.c) --
# see src/main.c's header comment for why. Compile those sources directly
# rather than the full SNESRECOMP_RUNNER_SOURCES list until the AOT hybrid
# tier is wired.
set(SC_DEVICE_SOURCES
    ${SNESRECOMP_ROOT}/runner/src/audio_trace.c
    ${SNESRECOMP_ROOT}/runner/src/crc32.c
    ${SNESRECOMP_ROOT}/runner/src/sha256.c
    ${SNESRECOMP_ROOT}/runner/src/util.c
    ${SNESRECOMP_ROOT}/runner/src/snes/apu.c
    ${SNESRECOMP_ROOT}/runner/src/snes/cart.c
    ${SNESRECOMP_ROOT}/runner/src/snes/cx4.c
    ${SNESRECOMP_ROOT}/runner/src/snes/cpu.c
    ${SNESRECOMP_ROOT}/runner/src/snes/dma.c
    ${SNESRECOMP_ROOT}/runner/src/snes/dsp.c
    ${SNESRECOMP_ROOT}/runner/src/snes/dsp1.c
    ${SNESRECOMP_ROOT}/runner/src/snes/dsp1_hle.c
    ${SNESRECOMP_ROOT}/runner/src/snes/joypad.c
    ${CMAKE_SOURCE_DIR}/src/sc_ppu.c
    ${SNESRECOMP_ROOT}/runner/src/snes/ppu_legacy.c
    ${SNESRECOMP_ROOT}/runner/src/snes/sa1.c
    ${SNESRECOMP_ROOT}/runner/src/snes/snes.c
    ${SNESRECOMP_ROOT}/runner/src/snes/snes_other.c
    ${SNESRECOMP_ROOT}/runner/src/snes/spc.c
    ${SNESRECOMP_ROOT}/runner/src/snes/superfx.c
    ${SNESRECOMP_ROOT}/runner/src/snes/ws_shadow.c
    ${SNESRECOMP_ROOT}/runner/src/snes/interp816.c
    ${SNESRECOMP_ROOT}/runner/src/snes/color_lut.c
)
target_sources(UrbanRecomp PRIVATE ${SC_DEVICE_SOURCES})

# Compatible per-address C execution, generated locally from the clean US
# ROM. This uses the live Interp816 register/bus ABI and returns at every
# original instruction boundary, so main.c's game hooks remain authoritative.
# It does not enable the incompatible AOT/fiber scheduler.
option(SC_PROGRAM "Use locally compiled native ROM instruction sites" ON)
if(SC_PROGRAM)
    file(GLOB SC_PROGRAM_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/src/program_gen/sc_program_bank*.c")
endif()
add_library(ScProgramRuntime STATIC src/sc_program.c ${SC_PROGRAM_SOURCES})
target_include_directories(ScProgramRuntime PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${SNESRECOMP_ROOT}/runner/src)
if(SC_PROGRAM_SOURCES)
    target_compile_definitions(ScProgramRuntime PRIVATE SC_PROGRAM_TIER)
    if(EXISTS "${CMAKE_SOURCE_DIR}/src/program_gen/sc_program_cold.h")
        target_compile_definitions(ScProgramRuntime PRIVATE SC_PROGRAM_FULL_ROM)
    endif()
    if(EXISTS "${CMAKE_SOURCE_DIR}/src/program_gen/sc_program_bank00_blocks_0.c")
        target_compile_definitions(ScProgramRuntime PRIVATE SC_PROGRAM_BLOCK_TIER)
    endif()
endif()
function(sc_configure_native_program target)
    target_link_libraries(${target} PRIVATE ScProgramRuntime)
endfunction()
sc_configure_native_program(UrbanRecomp)
option(SC_INTERPRETER_REFERENCE "Link the 65816 decoder for independent migration controls" OFF)
if(NOT SC_INTERPRETER_REFERENCE)
    if(NOT SC_PROGRAM_SOURCES OR NOT EXISTS "${CMAKE_SOURCE_DIR}/src/program_gen/sc_program_cold.h")
        message(FATAL_ERROR "Native execution requires generated program code. Run tools/compile_native_program.py with your verified US ROM, or explicitly select SC_INTERPRETER_REFERENCE=ON for an oracle build.")
    endif()
    get_target_property(SC_NATIVE_APP_SOURCES UrbanRecomp SOURCES)
    list(FILTER SC_NATIVE_APP_SOURCES EXCLUDE REGEX "/interp816\\.c$")
    set_property(TARGET UrbanRecomp PROPERTY SOURCES ${SC_NATIVE_APP_SOURCES})
    target_sources(UrbanRecomp PRIVATE src/sc_cpu_state.c)
    target_compile_definitions(UrbanRecomp PRIVATE SC_NATIVE_ONLY=1)
endif()
if(SC_INTERPRETER_REFERENCE AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND NOT MSVC AND NOT APPLE)
    target_sources(UrbanRecomp PRIVATE src/sc_interpreter_guard.c)
    target_link_options(UrbanRecomp PRIVATE "-Wl,--wrap=interp816_runOpcode")
endif()

# Let the compiler optimize the native simulation, map helpers and device
# scheduler together. This must be a target property: a cache variable alone
# does not enable LTO. Keep debug builds and unsupported toolchains usable.
option(SC_LTO "Enable supported link-time optimization for release builds" OFF)
if(SC_LTO)
    include(CheckIPOSupported)
    set(SC_IPO_LANGUAGES C)
    if(CMAKE_CXX_COMPILER_LOADED)
        list(APPEND SC_IPO_LANGUAGES CXX)
    endif()
    check_ipo_supported(RESULT SC_IPO_SUPPORTED OUTPUT SC_IPO_DIAGNOSTICS
                        LANGUAGES ${SC_IPO_LANGUAGES})
    if(SC_IPO_SUPPORTED)
        set_target_properties(UrbanRecomp PROPERTIES
            INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
            INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
        message(STATUS "UrbanRecomp: release link-time optimization enabled")
    else()
        message(STATUS "UrbanRecomp: link-time optimization unavailable; using ordinary compilation")
    endif()
endif()

# A trace build (-DSNESRECOMP_ENABLE_TRACE=ON) turns on the observability
# rings in cpu_trace.h, and those call into the debug server. This target
# hand-picks its sources instead of taking SNESRECOMP_RUNNER_SOURCES whole,
# so runner.cmake's own trace block never reaches it and the link fails on a
# dozen debug_server_* symbols. Add them here, for trace builds only.
if(SNESRECOMP_ENABLE_TRACE)
    target_sources(UrbanRecomp PRIVATE
        ${SNESRECOMP_ROOT}/runner/src/debug_server.c
        ${SNESRECOMP_ROOT}/runner/src/desktop/post_mortem.c)
endif()


if(NOT MSVC)
    target_compile_options(UrbanRecomp PRIVATE
        -w $<$<COMPILE_LANGUAGE:C>:-Wno-implicit-function-declaration>)
endif()

# Desktop SDL comes from the runner helper rather than a per-game
# find_package: it picks the backend (SDL3 by default, SDL2 via
# -DSNESRECOMP_SDL_BACKEND=SDL2), sets SNESRECOMP_SDL3 and SDL_MAIN_HANDLED,
# and copies the runtime DLL beside the exe on Windows. Rolling our own is
# how this project stayed on SDL2 without noticing SDL3 had become the
# default -- see docs/SDL_BACKENDS.md upstream.
# SDL3 is the default (runner.cmake's own default). It was pinned to SDL2 for
# a while after two defects reached a "verified" build through a headless gate
# that could not fail on them: SDL_Init's inverted return convention, and a
# black window from SDL3 defaulting textures to blending while this
# framebuffer carries no alpha. Both fixed, and the window checked by eye.
#
# Build the fallback with -DSNESRECOMP_SDL_BACKEND=SDL2.
#
# SDL GPU/Vulkan presentation and framebuffer dumps are independently verified.
# Shader readback is a test-only path; ordinary gameplay does not wait for it.
snesrecomp_target_sdl(UrbanRecomp)
if(SC_HAVE_LAUNCHER)
    # Box art and window icon are derived from assets/urbanrecomp_logo.png by
    # tools/make_brand_assets.py; src/sc_launcher.c names both paths.
    recomp_target_launcher_ui(UrbanRecomp CONSOLE snes
        BOXART ${CMAKE_SOURCE_DIR}/assets/boxart.jpg BOXART_NAME boxart.jpg)
    add_custom_command(TARGET UrbanRecomp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_SOURCE_DIR}/assets/icon.png
                $<TARGET_FILE_DIR:UrbanRecomp>/assets/img/icon.png
        VERBATIM)
endif()
target_compile_definitions(UrbanRecomp PRIVATE "SC_VERSION=\"${PROJECT_VERSION}\"")
# The executable's icon on Windows (Explorer, taskbar, every SDL window).
if(WIN32)
    target_sources(UrbanRecomp PRIVATE src/urbanrecomp.rc)
endif()
sc_configure_windows_app(UrbanRecomp)

# ── Linux desktop ───────────────────────────────────────────────────────────
#
# Every build sets its window icon from pixels compiled in (src/sc_icon.c).
# A desktop, though -- and on Wayland ONLY the desktop -- takes the icon of a
# running window from its desktop entry, matched by application ID. So on
# Linux `cmake --install` puts the program where it can find its assets, and
# registers it:
#
#   <prefix>/lib/urbanrecomp/          UrbanRecomp, assets/, sylt_graphics/
#   <prefix>/bin/urbanrecomp           starter; runs the game from the player's
#                                      data folder, ~/.local/share/urbanrecomp
#   <prefix>/share/applications/       io.github.blackerking.UrbanRecomp.desktop
#   <prefix>/share/icons/hicolor/      16x16 ... 512x512 icons
#   <prefix>/share/metainfo/           AppStream description (software centres)
#
# The window carries the same ID (ScSetAppIdentity), so the running game
# shows the icon in the dock and the task switcher. The desktop entry names
# the starter by its full path, so choose the prefix when configuring -- for
# the current user:
#
#   cmake -S . -B build -DCMAKE_INSTALL_PREFIX=$HOME/.local ...
#   cmake --build build --target UrbanRecomp && cmake --install build
#
# A build folder run in place still works as before: the game finds its
# assets next to the executable and keeps its files in the working directory.
if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    set(SC_APP_ID "io.github.blackerking.UrbanRecomp")
    set(SC_INSTALL_DIR "${CMAKE_INSTALL_FULL_LIBDIR}/urbanrecomp")
    file(RELATIVE_PATH SC_BIN_TO_GAME "${CMAKE_INSTALL_FULL_BINDIR}" "${SC_INSTALL_DIR}")
    set(SC_LAUNCH_SCRIPT "${CMAKE_INSTALL_FULL_BINDIR}/urbanrecomp")
    set(SC_LINUX "${CMAKE_SOURCE_DIR}/assets/linux")
    configure_file(${SC_LINUX}/urbanrecomp.desktop.in
                   ${CMAKE_BINARY_DIR}/linux/${SC_APP_ID}.desktop @ONLY)
    configure_file(${SC_LINUX}/urbanrecomp.metainfo.xml.in
                   ${CMAKE_BINARY_DIR}/linux/${SC_APP_ID}.metainfo.xml @ONLY)
    configure_file(${SC_LINUX}/urbanrecomp.sh.in
                   ${CMAKE_BINARY_DIR}/linux/urbanrecomp @ONLY NEWLINE_STYLE UNIX)

    install(TARGETS UrbanRecomp RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}/urbanrecomp)
    install(FILES ${CMAKE_SOURCE_DIR}/sylt_graphics/sylt_map.bin
                  ${CMAKE_SOURCE_DIR}/sylt_graphics/sylt_card.bin
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/urbanrecomp/sylt_graphics)
    if(SC_HAVE_LAUNCHER)
        # The same files recomp_target_launcher_ui() stages next to the build.
        install(FILES ${_RUI_COMMON_FONT_ASSETS}
                DESTINATION ${CMAKE_INSTALL_LIBDIR}/urbanrecomp/assets/fonts)
        install(FILES ${_RUI_COMMON_IMG_ASSETS} ${_RUI_CONSOLE_ASSETS_snes}
                      ${CMAKE_SOURCE_DIR}/assets/boxart.jpg
                      ${CMAKE_SOURCE_DIR}/assets/icon.png
                DESTINATION ${CMAKE_INSTALL_LIBDIR}/urbanrecomp/assets/img)
    endif()
    install(PROGRAMS ${CMAKE_BINARY_DIR}/linux/urbanrecomp
            DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES ${CMAKE_BINARY_DIR}/linux/${SC_APP_ID}.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES ${CMAKE_BINARY_DIR}/linux/${SC_APP_ID}.metainfo.xml
            DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
    foreach(size 16 24 32 48 64 128 256 512)
        install(FILES ${SC_LINUX}/icons/${size}.png
                DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
                RENAME ${SC_APP_ID}.png)
    endforeach()
    install(FILES ${CMAKE_SOURCE_DIR}/LICENSE ${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md
                  ${CMAKE_SOURCE_DIR}/README.md
            DESTINATION ${CMAKE_INSTALL_DOCDIR})
endif()

include(CTest)
if(BUILD_TESTING)
    set(SC_RENDER_TESTS sc_renderer_test)
    if(EXISTS "${RECOMP_UI_ROOT}/src/recomp_launcher.h")
    add_executable(sc_video_test tests/sc_video_test.c src/sc_video.c src/sc_mods.c)
    target_include_directories(sc_video_test PRIVATE src ${RECOMP_UI_ROOT}/src)
    add_test(NAME simcity_video COMMAND sc_video_test ${CMAKE_CURRENT_BINARY_DIR}/sc-test-video.ini)
    list(APPEND SC_RENDER_TESTS sc_video_test)
    endif()
    add_executable(sc_renderer_test tests/sc_renderer_test.c src/sc_renderer.c src/sc_terrain.c src/sc_native_ppu.c src/sc_obj.c src/sc_video.c
                   src/sc_selector.c src/sc_titlesign.c src/sc_world.c src/sc_mapgen.c)
    target_include_directories(sc_renderer_test PRIVATE src ${SNESRECOMP_RUNNER_INCLUDE_DIRS})
    add_test(NAME simcity_renderer COMMAND sc_renderer_test)
    add_executable(sc_scroll_renderer_test tests/sc_scroll_renderer_test.c src/sc_renderer.c src/sc_terrain.c src/sc_native_ppu.c src/sc_obj.c
                   src/sc_video.c src/sc_selector.c src/sc_titlesign.c src/sc_world.c src/sc_mapgen.c)
    target_include_directories(sc_scroll_renderer_test PRIVATE src ${SNESRECOMP_RUNNER_INCLUDE_DIRS})
    list(APPEND SC_RENDER_TESTS sc_scroll_renderer_test)
    add_test(NAME simcity_scroll_renderer COMMAND sc_scroll_renderer_test)
    add_executable(sc_terrain_test tests/sc_terrain_test.c src/sc_renderer.c src/sc_terrain.c src/sc_native_ppu.c src/sc_obj.c
                   src/sc_video.c src/sc_selector.c src/sc_titlesign.c src/sc_world.c src/sc_mapgen.c)
    target_include_directories(sc_terrain_test PRIVATE src ${SNESRECOMP_RUNNER_INCLUDE_DIRS})
    list(APPEND SC_RENDER_TESTS sc_terrain_test)
    add_test(NAME simcity_deferred_terrain COMMAND sc_terrain_test)
    add_executable(UrbanRecompGpuTest EXCLUDE_FROM_ALL tests/sc_terrain_test.c src/sc_renderer.c
                   src/sc_terrain.c src/sc_native_ppu.c src/sc_obj.c src/sc_gpu_terrain.c src/sc_gpu_fields.c src/sc_video.c src/sc_selector.c src/sc_titlesign.c src/sc_world.c src/sc_mapgen.c)
    target_include_directories(UrbanRecompGpuTest PRIVATE src ${CMAKE_BINARY_DIR} ${SNESRECOMP_RUNNER_INCLUDE_DIRS})
    target_compile_definitions(UrbanRecompGpuTest PRIVATE SC_TEST_GPU=1)
    snesrecomp_target_sdl(UrbanRecompGpuTest)
    if(NOT MSVC)
        target_link_libraries(UrbanRecompGpuTest PRIVATE m)
    endif()
    foreach(test ${SC_RENDER_TESTS})
        if(MSVC)
            target_compile_options(${test} PRIVATE /UNDEBUG)
        else()
            target_compile_options(${test} PRIVATE -UNDEBUG -Wall -Wextra)
            target_link_libraries(${test} PRIVATE m)
            if(MINGW)
                target_link_options(${test} PRIVATE -static)
            endif()
        endif()
    endforeach()
endif()

# ── AOT link probe (step 1 of the CpuState migration) ────────────────────────
#
# Optional target, NOT built by default and NOT part of the shipping
# executable. It links the generated banks in src/gen against the shared
# AOT/CpuState runtime and reports the dispatch table -- see src/aot_probe.c
# for why this exists before touching src/main.c.
#
#   cmake --build build --target UrbanRecompAOTProbe
#
# It is excluded from ALL so a broken or stale src/gen can never break the
# normal build, and it is skipped entirely when src/gen has not been generated.
file(GLOB SC_GEN_SOURCES ${CMAKE_SOURCE_DIR}/src/gen/*.c)

# SC_AOT=OFF builds without the generated banks even when src/gen exists.
# Release packages use it: src/gen is compiled from the ROM, so a binary that
# links it carries ROM-derived code, and the interpreter -- the default at run
# time anyway -- needs none of it.
option(SC_AOT "Link the AOT tier from src/gen when it is present" ON)
if(NOT SC_AOT)
    set(SC_GEN_SOURCES "")
endif()

# ── AOT is the standard build ───────────────────────────────────────────────
#
# When the generated banks are present, UrbanRecomp IS the AOT build: the
# compiled bodies and the HLEs are linked in, and SC_FIBER=1 runs the guest
# through them. That is what makes the map generator HLE at 01:f1ed run,
# which is the point of having an AOT tier at all -- it turns ~800 frames of
# the SNES CPU grinding through 03:d840 into less than one.
#
# Without SC_FIBER the interpreter runs every opcode -- the default, and the
# correctness baseline every --qualify number rests on.
#
# Without src/gen -- a fresh clone before tools/regen.sh -- this whole block is
# skipped and the target builds interpreter-only exactly as it did before.
if(SC_GEN_SOURCES)
    target_sources(UrbanRecomp PRIVATE
        ${SC_GEN_SOURCES}
        src/sc_hle.c
        src/sc_fiber.c
        src/sc_fiberdrive.c
        ${SNESRECOMP_ROOT}/runner/src/cpu_state.c
        ${SNESRECOMP_ROOT}/runner/src/common_cpu_infra.c
        ${SNESRECOMP_ROOT}/runner/src/common_rtl.c
        ${SNESRECOMP_ROOT}/runner/src/cpu_trace.c
        ${SNESRECOMP_ROOT}/runner/src/ppu_dma_trace.c
        ${SNESRECOMP_ROOT}/runner/src/recomp_hw.c
        ${SNESRECOMP_ROOT}/runner/src/host_report.c
        ${SNESRECOMP_ROOT}/runner/src/framedump.c
        ${SNESRECOMP_ROOT}/runner/src/snes/interp_bridge.c
        ${SNESRECOMP_ROOT}/runner/src/snes/tier2_capture.c
        ${SNESRECOMP_ROOT}/runner/src/snes/msu1.c
    )
    target_include_directories(UrbanRecomp PRIVATE
        ${SNESRECOMP_ROOT}/runner/src/desktop)
    target_compile_definitions(UrbanRecomp PRIVATE SC_AOT_TIER)
    if(MSVC)
        # cpu_state.c's weak-marked fallback guard table is a strong definition
        # under MSVC, and common_rtl.c redefines a handful of APU helpers that
        # main.c also carries. See the note on UrbanRecompAOTProbe.
        target_link_options(UrbanRecomp PRIVATE /FORCE:MULTIPLE)
    else()
        target_compile_options(UrbanRecomp PRIVATE
            -w $<$<COMPILE_LANGUAGE:C>:-Wno-implicit-function-declaration>)
    endif()
endif()

if(SC_GEN_SOURCES)
    add_executable(UrbanRecompAOTProbe EXCLUDE_FROM_ALL
        src/aot_probe.c
        ${SC_GEN_SOURCES}
        src/sc_hle.c
        src/sc_mapgen.c
        src/sc_decomp.c
        src/sc_fiber.c
        # src/sc_fiberdrive.c -- NOT linked here.
        #
        # Nothing in these targets uses the ScFiberDrive_* API; only
        # src/main.c does, and that is not compiled into them. Linking it
        # anyway dragged in sc_advance_until_input_ready(), which lives in
        # main.c, so both targets failed with LNK2019.
        #
        # Stubbing that symbol would be wrong: it drains autoJoyTimer before
        # the guest is handed a frame, and skipping it is exactly how the
        # first fiber attempt deadlocked -- the game spins on $4212 at
        # 00:9280 and nothing advances the beam while the guest holds the
        # CPU. A stub would link and then hang.
        ${SNESRECOMP_ROOT}/runner/src/cpu_state.c
        ${SNESRECOMP_ROOT}/runner/src/common_cpu_infra.c
        ${SNESRECOMP_ROOT}/runner/src/common_rtl.c
        ${SNESRECOMP_ROOT}/runner/src/cpu_trace.c
        ${SNESRECOMP_ROOT}/runner/src/snes/interp_bridge.c
        ${SNESRECOMP_ROOT}/runner/src/snes/tier2_capture.c
        ${SNESRECOMP_ROOT}/runner/src/snes/msu1.c
        ${SNESRECOMP_ROOT}/runner/src/recomp_hw.c
        ${SNESRECOMP_ROOT}/runner/src/ppu_dma_trace.c
        ${SNESRECOMP_ROOT}/runner/src/host_report.c
        ${SNESRECOMP_ROOT}/runner/src/framedump.c
        ${SC_DEVICE_SOURCES}
    )
    if(MSVC)
        # WORKAROUND, not a solution. cpu_state.c ships an empty fallback
        # g_ram_routine_guards[] marked __attribute__((weak)) -- a GCC/Clang
        # construct MSVC ignores -- so its definition collides with the strong
        # one every generated dispatch_v2.c emits. The portable fix upstream is
        # __declspec(selectany) on MSVC; until then the probe tolerates the
        # duplicate. Do NOT carry this into the shipping target: it would also
        # hide genuine duplicate-symbol bugs.
        target_link_options(UrbanRecompAOTProbe PRIVATE /FORCE:MULTIPLE)
    endif()
    target_include_directories(UrbanRecompAOTProbe PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/recomp
        ${SNESRECOMP_RUNNER_INCLUDE_DIRS}
        # common_rtl.c includes "config.h", which lives in the desktop runner
        # dir rather than on the shared runner include path.
        ${SNESRECOMP_ROOT}/runner/src/desktop
    )
    if(NOT MSVC)
        target_compile_options(UrbanRecompAOTProbe PRIVATE
            -w $<$<COMPILE_LANGUAGE:C>:-Wno-implicit-function-declaration>)
    endif()
    # config.h -> sdl_compat.h -> SDL.h, so the probe needs SDL's headers even
    # though it opens no window.
    snesrecomp_target_sdl(UrbanRecompAOTProbe)
endif()

# -- AOT-tier build of the real host (migration step 2) ----------------------
#
# Same src/main.c as the shipping target, but linked together with the
# generated banks and the AOT/CpuState runtime. Behaviour is identical -- it
# still runs 100% on interp816 -- so this proves only that the two tiers can
# COEXIST in one binary: shared g_ram, no duplicate symbols, no ABI clash.
# Routing execution through the compiled bodies is step 3.
#
#   cmake --build build --target UrbanRecompAOT
# NOTE: this now builds the same content as the default target, which links the
# AOT tier itself. Kept because docs/AOT_LLE.md and tools/gen_align_check.py
# name it, and because having a target that is unambiguously the AOT build is
# useful when the default one is configured without src/gen.
if(SC_GEN_SOURCES)
    add_executable(UrbanRecompAOT EXCLUDE_FROM_ALL
        src/main.c
        src/sc_world.c
        src/sc_world_guest.c src/sc_postpass.c src/sc_sweep.c src/sc_power_traversal.c src/sc_zoning.c src/sc_smoothing.c src/sc_service.c src/sc_density.c src/sc_tile_lookup.c src/sc_land.c src/sc_transport.c src/sc_infrastructure.c
        src/sc_development.c
        src/sc_population.c
        src/sc_mouse_ui.c
        src/sc_construction.c src/sc_refresh_clock.c src/sc_power_refresh.c
        src/sc_clipboard.c
        src/sc_math.c
        src/sc_wait.c
        src/sc_sprite.c
        src/sc_music.c src/sc_pcm.c
        src/sc_launcher.c
        src/sc_sram.c
        src/sc_test_city.c
        src/sc_icon.c
        src/sc_mapview.c
        src/sc_vehicles.c
        src/sc_fleet.c
        src/sc_selector.c
        src/sc_titlesign.c
        ${SC_GEN_SOURCES}
        src/sc_hle.c
        src/sc_mapgen.c
        src/sc_decomp.c
        src/sc_fiber.c
        src/sc_fiberdrive.c
    src/sc_video.c
    $<$<BOOL:${SC_HAVE_LAUNCHER}>:${CMAKE_SOURCE_DIR}/src/sc_mods.c>
    src/sc_renderer.c
    src/sc_terrain.c src/sc_native_ppu.c src/sc_obj.c src/sc_gpu_terrain.c src/sc_gpu_fields.c
        ${SNESRECOMP_ROOT}/runner/src/cpu_state.c
        ${SNESRECOMP_ROOT}/runner/src/common_cpu_infra.c
        ${SNESRECOMP_ROOT}/runner/src/common_rtl.c
        ${SNESRECOMP_ROOT}/runner/src/cpu_trace.c
        ${SNESRECOMP_ROOT}/runner/src/ppu_dma_trace.c
        ${SNESRECOMP_ROOT}/runner/src/recomp_hw.c
        ${SNESRECOMP_ROOT}/runner/src/host_report.c
        ${SNESRECOMP_ROOT}/runner/src/framedump.c
        ${SNESRECOMP_ROOT}/runner/src/snes/interp_bridge.c
        ${SNESRECOMP_ROOT}/runner/src/snes/tier2_capture.c
        ${SNESRECOMP_ROOT}/runner/src/snes/msu1.c
        ${SC_DEVICE_SOURCES}
    )
    target_include_directories(UrbanRecompAOT PRIVATE
        ${CMAKE_BINARY_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/recomp
        ${SNESRECOMP_RUNNER_INCLUDE_DIRS}
        ${SNESRECOMP_ROOT}/runner/src/desktop
    )
    target_compile_definitions(UrbanRecompAOT PRIVATE
        SC_AOT_TIER "SC_VERSION=\"${PROJECT_VERSION}\"")
    if(MSVC)
        # See the note on UrbanRecompAOTProbe: cpu_state.c's weak-marked fallback
        # guard table is a strong definition under MSVC.
        target_link_options(UrbanRecompAOT PRIVATE /FORCE:MULTIPLE)
    else()
        target_compile_options(UrbanRecompAOT PRIVATE
            -w $<$<COMPILE_LANGUAGE:C>:-Wno-implicit-function-declaration>)
    endif()
    snesrecomp_target_sdl(UrbanRecompAOT)
    if(WIN32)
        target_sources(UrbanRecompAOT PRIVATE src/urbanrecomp.rc)
    endif()
    sc_configure_windows_app(UrbanRecompAOT)
endif()

# -- AOT differential harness (migration step 3a) ----------------------------
#   cmake --build build --target UrbanRecompAOTDiff
if(SC_GEN_SOURCES)
    add_executable(UrbanRecompAOTDiff EXCLUDE_FROM_ALL
        src/aot_diff.c
        ${SC_GEN_SOURCES}
        src/sc_hle.c
        src/sc_mapgen.c
        src/sc_decomp.c
        src/sc_fiber.c
        # src/sc_fiberdrive.c -- NOT linked here.
        #
        # Nothing in these targets uses the ScFiberDrive_* API; only
        # src/main.c does, and that is not compiled into them. Linking it
        # anyway dragged in sc_advance_until_input_ready(), which lives in
        # main.c, so both targets failed with LNK2019.
        #
        # Stubbing that symbol would be wrong: it drains autoJoyTimer before
        # the guest is handed a frame, and skipping it is exactly how the
        # first fiber attempt deadlocked -- the game spins on $4212 at
        # 00:9280 and nothing advances the beam while the guest holds the
        # CPU. A stub would link and then hang.
        ${SNESRECOMP_ROOT}/runner/src/cpu_state.c
        ${SNESRECOMP_ROOT}/runner/src/common_cpu_infra.c
        ${SNESRECOMP_ROOT}/runner/src/common_rtl.c
        ${SNESRECOMP_ROOT}/runner/src/cpu_trace.c
        ${SNESRECOMP_ROOT}/runner/src/ppu_dma_trace.c
        ${SNESRECOMP_ROOT}/runner/src/recomp_hw.c
        ${SNESRECOMP_ROOT}/runner/src/host_report.c
        ${SNESRECOMP_ROOT}/runner/src/framedump.c
        ${SNESRECOMP_ROOT}/runner/src/snes/interp_bridge.c
        ${SNESRECOMP_ROOT}/runner/src/snes/tier2_capture.c
        ${SNESRECOMP_ROOT}/runner/src/snes/msu1.c
        ${SC_DEVICE_SOURCES}
    )
    target_include_directories(UrbanRecompAOTDiff PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/recomp
        ${SNESRECOMP_RUNNER_INCLUDE_DIRS}
        ${SNESRECOMP_ROOT}/runner/src/desktop
    )
    if(MSVC)
        target_link_options(UrbanRecompAOTDiff PRIVATE /FORCE:MULTIPLE)
    else()
        target_compile_options(UrbanRecompAOTDiff PRIVATE
            -w $<$<COMPILE_LANGUAGE:C>:-Wno-implicit-function-declaration>)
    endif()
    snesrecomp_target_sdl(UrbanRecompAOTDiff)
endif()

# -- Fiber layer self-test ---------------------------------------------------
# Standalone: no ROM, no runtime, no generated code. Coroutine bugs are far
# easier to find here than underneath a running 65816 interpreter.
#   cmake --build build --target UrbanRecompFiberTest && ./build/Release/UrbanRecompFiberTest
add_executable(UrbanRecompFiberTest EXCLUDE_FROM_ALL
    tests/fiber_test.c
    src/sc_fiber.c
)
target_include_directories(UrbanRecompFiberTest PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_executable(UrbanRecompPointerTest EXCLUDE_FROM_ALL tests/sc_pointer_test.c src/sc_video.c)
add_executable(sc_beam_test tests/sc_beam_test.c)
target_include_directories(sc_beam_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(BUILD_TESTING)
    add_test(NAME simcity_beam_events COMMAND sc_beam_test)
endif()
add_executable(UrbanRecompPpuTest EXCLUDE_FROM_ALL tests/sc_ppu_test.c src/sc_ppu.c src/sc_terrain.c src/sc_native_ppu.c src/sc_obj.c
    ${SNESRECOMP_ROOT}/runner/src/snes/ppu_legacy.c
    ${SNESRECOMP_ROOT}/runner/src/snes/ws_shadow.c)
target_include_directories(UrbanRecompPpuTest PRIVATE ${CMAKE_SOURCE_DIR}/src
    ${SNESRECOMP_ROOT}/runner/src)
if(BUILD_TESTING)
    set_target_properties(UrbanRecompPpuTest PROPERTIES EXCLUDE_FROM_ALL FALSE)
    add_test(NAME simcity_skipped_ppu COMMAND UrbanRecompPpuTest)
endif()
if(NOT MSVC)
    target_link_libraries(UrbanRecompPpuTest PRIVATE m)
endif()
target_include_directories(UrbanRecompPointerTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(NOT MSVC)
    target_link_libraries(UrbanRecompPointerTest PRIVATE m)
endif()

add_executable(UrbanRecompDevelopmentTest EXCLUDE_FROM_ALL
    tests/sc_development_test.c src/sc_development.c src/sc_development_batches.c src/sc_population.c src/sc_math.c
    src/sc_world.c src/sc_world_guest.c src/sc_postpass.c src/sc_sweep.c src/sc_power_traversal.c src/sc_zoning.c src/sc_smoothing.c src/sc_service.c src/sc_density.c src/sc_tile_lookup.c src/sc_land.c src/sc_transport.c src/sc_infrastructure.c src/sc_mapgen.c
    ${SNESRECOMP_ROOT}/runner/src/snes/interp816.c)
target_include_directories(UrbanRecompDevelopmentTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${SNESRECOMP_ROOT}/runner/src)

add_executable(UrbanRecompTestCityTest EXCLUDE_FROM_ALL
    tests/sc_test_city_test.c src/sc_test_city.c src/sc_sram.c src/sc_population.c
    src/sc_construction.c src/sc_world.c src/sc_world_guest.c src/sc_postpass.c src/sc_sweep.c
    src/sc_power_traversal.c src/sc_zoning.c src/sc_smoothing.c src/sc_service.c
    src/sc_density.c src/sc_tile_lookup.c src/sc_land.c src/sc_transport.c src/sc_infrastructure.c src/sc_mapgen.c
    ${SNESRECOMP_ROOT}/runner/src/snes/interp816.c)
target_include_directories(UrbanRecompTestCityTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${SNESRECOMP_ROOT}/runner/src)
if(MSVC)
    target_compile_options(UrbanRecompTestCityTest PRIVATE /UNDEBUG)
else()
    target_compile_options(UrbanRecompTestCityTest PRIVATE -UNDEBUG)
endif()

add_executable(UrbanRecompConstructionTest EXCLUDE_FROM_ALL
    tests/sc_construction_test.c src/sc_construction.c
    src/sc_world.c src/sc_world_guest.c src/sc_postpass.c src/sc_sweep.c src/sc_power_traversal.c src/sc_zoning.c src/sc_smoothing.c src/sc_service.c src/sc_density.c src/sc_tile_lookup.c src/sc_land.c src/sc_transport.c src/sc_infrastructure.c src/sc_mapgen.c
    ${SNESRECOMP_ROOT}/runner/src/snes/interp816.c)
target_include_directories(UrbanRecompConstructionTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${SNESRECOMP_ROOT}/runner/src)

add_executable(UrbanRecompClipboardTest EXCLUDE_FROM_ALL
    tests/sc_clipboard_test.c src/sc_clipboard.c src/sc_construction.c
    src/sc_world.c src/sc_world_guest.c src/sc_postpass.c src/sc_sweep.c src/sc_power_traversal.c src/sc_zoning.c src/sc_smoothing.c src/sc_service.c src/sc_density.c src/sc_tile_lookup.c src/sc_land.c src/sc_transport.c src/sc_infrastructure.c src/sc_mapgen.c src/sc_population.c
    ${SNESRECOMP_ROOT}/runner/src/snes/interp816.c)
target_include_directories(UrbanRecompClipboardTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${SNESRECOMP_ROOT}/runner/src)

add_executable(UrbanRecompMathTest EXCLUDE_FROM_ALL
    tests/sc_math_test.c src/sc_math.c
    ${SNESRECOMP_ROOT}/runner/src/snes/interp816.c)
target_include_directories(UrbanRecompMathTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${SNESRECOMP_ROOT}/runner/src)

add_executable(UrbanRecompMouseUiTest EXCLUDE_FROM_ALL tests/sc_mouse_ui_test.c src/sc_mouse_ui.c)
target_include_directories(UrbanRecompMouseUiTest PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_executable(UrbanRecompPopulationTest EXCLUDE_FROM_ALL
    tests/sc_population_test.c src/sc_population.c src/sc_world.c src/sc_mapgen.c
    ${SNESRECOMP_ROOT}/runner/src/snes/interp816.c)
target_include_directories(UrbanRecompPopulationTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${SNESRECOMP_ROOT}/runner/src)

add_executable(UrbanRecompMapGenTest EXCLUDE_FROM_ALL tests/sc_mapgen_test.c src/sc_mapgen.c)
add_executable(UrbanRecompMusicTest EXCLUDE_FROM_ALL
    tests/sc_music_test.c src/sc_music.c src/sc_pcm.c
    ${SNESRECOMP_ROOT}/runner/src/snes/apu.c
    ${SNESRECOMP_ROOT}/runner/src/snes/spc.c
    ${SNESRECOMP_ROOT}/runner/src/snes/dsp.c
    ${SNESRECOMP_ROOT}/runner/src/snes/dsp_shadow.c
    ${SNESRECOMP_ROOT}/runner/src/snes/audio_shadow.c
    ${SNESRECOMP_ROOT}/runner/src/audio_trace.c)
target_include_directories(UrbanRecompMusicTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${SNESRECOMP_ROOT}/runner/src)
snesrecomp_target_sdl(UrbanRecompMusicTest)

add_executable(UrbanRecompPcmTest EXCLUDE_FROM_ALL tests/sc_pcm_test.c src/sc_pcm.c)
target_include_directories(UrbanRecompPcmTest PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Imported soundtrack assets are optional and local. They are not bundled in
# the repository or published by the build. A portable private build finds
# the pack next to its executable, independently of its working directory.
if(EXISTS "${CMAKE_SOURCE_DIR}/music/restored/scity-msu1-19.pcm")
    add_custom_command(TARGET UrbanRecomp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/music/restored" "$<TARGET_FILE_DIR:UrbanRecomp>/music/restored")
endif()
target_include_directories(UrbanRecompMapGenTest PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_executable(UrbanRecompWorldTest EXCLUDE_FROM_ALL
    tests/sc_world_test.c src/sc_world.c src/sc_world_guest.c src/sc_postpass.c src/sc_sweep.c src/sc_power_traversal.c src/sc_zoning.c src/sc_smoothing.c src/sc_service.c src/sc_density.c src/sc_tile_lookup.c src/sc_land.c src/sc_transport.c src/sc_infrastructure.c src/sc_mapgen.c
    src/sc_population.c
    ${SNESRECOMP_ROOT}/runner/src/snes/interp816.c)
target_include_directories(UrbanRecompWorldTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${SNESRECOMP_ROOT}/runner/src)
sc_configure_native_program(UrbanRecompWorldTest)

# Qualify host preparation independently of generated game instructions.
# Keep this small oracle build available while the longer family suite runs.
get_target_property(SC_WORLD_PREPARATION_SOURCES UrbanRecompWorldTest SOURCES)
list(FILTER SC_WORLD_PREPARATION_SOURCES EXCLUDE REGEX "program_gen/")
add_executable(UrbanRecompWorldPreparationTest EXCLUDE_FROM_ALL ${SC_WORLD_PREPARATION_SOURCES} src/sc_program.c)
target_include_directories(UrbanRecompWorldPreparationTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${SNESRECOMP_ROOT}/runner/src)
target_compile_definitions(UrbanRecompWorldPreparationTest PRIVATE SC_WORLD_PREPARATION_ONLY=1)

if(SC_PROGRAM_SOURCES)
    add_executable(UrbanRecompProgramTest EXCLUDE_FROM_ALL
        tests/sc_program_test.c ${SNESRECOMP_ROOT}/runner/src/snes/interp816.c)
    target_include_directories(UrbanRecompProgramTest PRIVATE
        ${CMAKE_SOURCE_DIR}/src ${SNESRECOMP_ROOT}/runner/src)
    sc_configure_native_program(UrbanRecompProgramTest)
    if(MSVC)
        target_compile_options(UrbanRecompProgramTest PRIVATE /UNDEBUG)
    else()
        target_compile_options(UrbanRecompProgramTest PRIVATE -UNDEBUG)
    endif()
endif()

add_library(ScCpuStateOracle OBJECT EXCLUDE_FROM_ALL
    ${SNESRECOMP_ROOT}/runner/src/snes/interp816.c)
target_include_directories(ScCpuStateOracle PRIVATE ${SNESRECOMP_ROOT}/runner/src)
target_compile_definitions(ScCpuStateOracle PRIVATE
    interp816_init=ScCpuOracleInit interp816_free=ScCpuOracleFree
    interp816_reset=ScCpuOracleReset interp816_saveload=ScCpuOracleSaveLoad
    interp816_setFlags=ScCpuOracleSetFlags interp816_getFlags=ScCpuOracleGetFlags
    interp816_set_brk_hook_enabled=ScCpuOracleBrkHook
    interp816_runOpcode=ScCpuOracleRun interp816_insns_total=ScCpuOracleInstructions
    interp816_cycles_total=ScCpuOracleCycles g_interp816_cur_pc=ScCpuOracleCurrentPc)
add_executable(UrbanRecompCpuStateTest EXCLUDE_FROM_ALL
    tests/sc_cpu_state_test.c src/sc_cpu_state.c $<TARGET_OBJECTS:ScCpuStateOracle>)
target_include_directories(UrbanRecompCpuStateTest PRIVATE ${SNESRECOMP_ROOT}/runner/src)
if(MSVC)
    target_compile_options(UrbanRecompCpuStateTest PRIVATE /UNDEBUG)
else()
    target_compile_options(UrbanRecompCpuStateTest PRIVATE -UNDEBUG)
endif()

add_executable(UrbanRecompPowerRefreshTest EXCLUDE_FROM_ALL
    tests/sc_power_refresh_test.c src/sc_refresh_clock.c src/sc_power_refresh.c src/sc_construction.c
    src/sc_world.c src/sc_world_guest.c src/sc_postpass.c src/sc_sweep.c src/sc_power_traversal.c src/sc_zoning.c src/sc_smoothing.c src/sc_service.c src/sc_density.c src/sc_tile_lookup.c src/sc_land.c src/sc_transport.c src/sc_infrastructure.c src/sc_mapgen.c
    ${SNESRECOMP_ROOT}/runner/src/snes/interp816.c)
target_include_directories(UrbanRecompPowerRefreshTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${SNESRECOMP_ROOT}/runner/src)

add_executable(UrbanRecompJourneyTest EXCLUDE_FROM_ALL
    tests/sc_journey_test.c src/sc_journey.c src/sc_world.c src/sc_mapgen.c
    src/sc_construction.c src/sc_world_guest.c src/sc_postpass.c src/sc_sweep.c src/sc_power_traversal.c src/sc_zoning.c src/sc_smoothing.c src/sc_service.c src/sc_density.c src/sc_tile_lookup.c src/sc_land.c src/sc_transport.c src/sc_infrastructure.c
    ${SNESRECOMP_ROOT}/runner/src/snes/interp816.c)
target_include_directories(UrbanRecompJourneyTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${SNESRECOMP_ROOT}/runner/src)

add_executable(UrbanRecompWaitTest EXCLUDE_FROM_ALL
    tests/sc_wait_test.c src/sc_wait.c
    ${SNESRECOMP_ROOT}/runner/src/snes/interp816.c)
target_include_directories(UrbanRecompWaitTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${SNESRECOMP_ROOT}/runner/src)

add_executable(UrbanRecompSpriteTest EXCLUDE_FROM_ALL
    tests/sc_sprite_test.c src/sc_sprite.c
    ${SNESRECOMP_ROOT}/runner/src/snes/interp816.c)
target_include_directories(UrbanRecompSpriteTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${SNESRECOMP_ROOT}/runner/src)

# Whole-field compute uses the same Vulkan device as presentation. This target
# requires a functioning SDL3 GPU renderer and is run explicitly on GPU hosts.
if(SNESRECOMP_SDL_BACKEND STREQUAL "SDL3")
    add_executable(UrbanRecompGpuFieldsTest EXCLUDE_FROM_ALL tests/sc_gpu_fields_test.c
        src/sc_gpu_fields.c src/sc_gpu_terrain.c src/sc_renderer.c src/sc_terrain.c
        src/sc_native_ppu.c src/sc_obj.c src/sc_video.c src/sc_selector.c src/sc_titlesign.c
        src/sc_world.c src/sc_mapgen.c)
    target_include_directories(UrbanRecompGpuFieldsTest PRIVATE src ${CMAKE_BINARY_DIR} ${SNESRECOMP_RUNNER_INCLUDE_DIRS})
    snesrecomp_target_sdl(UrbanRecompGpuFieldsTest)
    if(NOT MSVC)
        target_link_libraries(UrbanRecompGpuFieldsTest PRIVATE m)
    endif()
endif()

add_executable(UrbanRecompPostpassTest EXCLUDE_FROM_ALL
    tests/sc_postpass_test.c src/sc_world.c src/sc_world_guest.c src/sc_postpass.c src/sc_sweep.c
    src/sc_power_traversal.c src/sc_zoning.c src/sc_smoothing.c src/sc_service.c
    src/sc_density.c src/sc_tile_lookup.c src/sc_land.c src/sc_transport.c src/sc_infrastructure.c src/sc_mapgen.c
    ${SNESRECOMP_ROOT}/runner/src/snes/interp816.c)
target_include_directories(UrbanRecompPostpassTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${SNESRECOMP_ROOT}/runner/src)

add_executable(UrbanRecompSweepTest EXCLUDE_FROM_ALL
    tests/sc_sweep_test.c src/sc_world.c src/sc_world_guest.c src/sc_postpass.c src/sc_sweep.c
    src/sc_power_traversal.c src/sc_zoning.c src/sc_smoothing.c src/sc_service.c
    src/sc_density.c src/sc_tile_lookup.c src/sc_land.c src/sc_transport.c src/sc_infrastructure.c src/sc_mapgen.c
    ${SNESRECOMP_ROOT}/runner/src/snes/interp816.c)
target_include_directories(UrbanRecompSweepTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${SNESRECOMP_ROOT}/runner/src)

foreach(target UrbanRecompAOT UrbanRecompDevelopmentTest UrbanRecompConstructionTest UrbanRecompTestCityTest
        UrbanRecompClipboardTest UrbanRecompPowerRefreshTest UrbanRecompJourneyTest
        UrbanRecompPostpassTest UrbanRecompSweepTest)
    if(TARGET ${target})
        sc_configure_native_program(${target})
    endif()
endforeach()

get_target_property(SC_INFRASTRUCTURE_TEST_SOURCES UrbanRecompSweepTest SOURCES)
list(FILTER SC_INFRASTRUCTURE_TEST_SOURCES EXCLUDE REGEX "program_gen/|tests/sc_sweep_test.c|src/sc_program.c")
add_executable(UrbanRecompInfrastructureTest EXCLUDE_FROM_ALL
    tests/sc_infrastructure_test.c ${SC_INFRASTRUCTURE_TEST_SOURCES} src/sc_program.c)
target_include_directories(UrbanRecompInfrastructureTest PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${SNESRECOMP_ROOT}/runner/src)

add_executable(UrbanRecompFleetTest EXCLUDE_FROM_ALL tests/sc_fleet_test.c src/sc_fleet.c src/sc_world.c src/sc_mapgen.c)
target_include_directories(UrbanRecompFleetTest PRIVATE src)
