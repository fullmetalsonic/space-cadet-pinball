cmake_minimum_required(VERSION 3.22)
project(SpaceCadetPinball LANGUAGES C CXX)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
option(PINBALL_FETCH_DEPENDENCIES "Build pinned SDL sources" ON)
set(PINBALL_ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../assets" CACHE PATH "Local game/font assets")

if(NOT EMSCRIPTEN)
    if(PINBALL_FETCH_DEPENDENCIES)
        include(FetchContent)
        set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
        set(SDL_SHARED ON CACHE BOOL "" FORCE)
        set(SDL_STATIC OFF CACHE BOOL "" FORCE)
        set(SDL_TEST OFF CACHE BOOL "" FORCE)
        set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
        set(SDL2MIXER_SAMPLES OFF CACHE BOOL "" FORCE)
        set(SDL2MIXER_VENDORED OFF CACHE BOOL "" FORCE)
        set(SDL2MIXER_INSTALL OFF CACHE BOOL "" FORCE)
        foreach(codec FLAC GME MOD MP3 OPUS WAVPACK CMD MIDI_FLUIDSYNTH)
            set(SDL2MIXER_${codec} OFF CACHE BOOL "" FORCE)
        endforeach()
        set(SDL2MIXER_VORBIS "" CACHE STRING "" FORCE)
        set(SDL2MIXER_WAVE ON CACHE BOOL "" FORCE)
        if(ANDROID)
            # Android music is rendered by SoftwareMidi through Mix_HookMusic.
            set(SDL2MIXER_MIDI OFF CACHE BOOL "" FORCE)
            set(SDL2MIXER_MIDI_TIMIDITY OFF CACHE BOOL "" FORCE)
        else()
            set(SDL2MIXER_MIDI ON CACHE BOOL "" FORCE)
            set(SDL2MIXER_MIDI_TIMIDITY ON CACHE BOOL "" FORCE)
        endif()
        FetchContent_Declare(SDL2 URL https://github.com/libsdl-org/SDL/archive/refs/tags/release-2.30.12.tar.gz)
        FetchContent_MakeAvailable(SDL2)
        FetchContent_Declare(SDL2_mixer URL https://github.com/libsdl-org/SDL_mixer/archive/refs/tags/release-2.8.1.tar.gz)
        FetchContent_MakeAvailable(SDL2_mixer)
    else()
        find_package(SDL2 CONFIG REQUIRED)
        find_package(SDL2_mixer CONFIG REQUIRED)
    endif()
endif()

set(SOURCE_FILES
        SpaceCadetPinball/control.cpp
        SpaceCadetPinball/control.h
        SpaceCadetPinball/EmbeddedData.cpp
        SpaceCadetPinball/EmbeddedData.h
        SpaceCadetPinball/font_selection.cpp
        SpaceCadetPinball/font_selection.h
        SpaceCadetPinball/fullscrn.cpp
        SpaceCadetPinball/fullscrn.h
        SpaceCadetPinball/gdrv.cpp
        SpaceCadetPinball/gdrv.h
        SpaceCadetPinball/GroupData.cpp
        SpaceCadetPinball/GroupData.h
        SpaceCadetPinball/high_score.cpp
        SpaceCadetPinball/high_score.h
        SpaceCadetPinball/loader.cpp
        SpaceCadetPinball/loader.h
        SpaceCadetPinball/maths.cpp
        SpaceCadetPinball/maths.h
        SpaceCadetPinball/midi.cpp
        SpaceCadetPinball/midi.h
        SpaceCadetPinball/software_midi.cpp
        SpaceCadetPinball/software_midi.h
        SpaceCadetPinball/nudge.cpp
        SpaceCadetPinball/nudge.h
        SpaceCadetPinball/options.cpp
        SpaceCadetPinball/options.h
        SpaceCadetPinball/partman.cpp
        SpaceCadetPinball/partman.h
        SpaceCadetPinball/pb.cpp
        SpaceCadetPinball/pb.h
        SpaceCadetPinball/pch.h
        SpaceCadetPinball/proj.cpp
        SpaceCadetPinball/proj.h
        SpaceCadetPinball/render.cpp
        SpaceCadetPinball/render.h
        SpaceCadetPinball/score.cpp
        SpaceCadetPinball/score.h
        SpaceCadetPinball/Sound.cpp
        SpaceCadetPinball/Sound.h
        SpaceCadetPinball/SpaceCadetPinball.cpp
        SpaceCadetPinball/TBall.cpp
        SpaceCadetPinball/TBall.h
        SpaceCadetPinball/TBlocker.cpp
        SpaceCadetPinball/TBlocker.h
        SpaceCadetPinball/TBumper.cpp
        SpaceCadetPinball/TBumper.h
        SpaceCadetPinball/TCircle.cpp
        SpaceCadetPinball/TCircle.h
        SpaceCadetPinball/TCollisionComponent.cpp
        SpaceCadetPinball/TCollisionComponent.h
        SpaceCadetPinball/TComponentGroup.cpp
        SpaceCadetPinball/TComponentGroup.h
        SpaceCadetPinball/TDemo.cpp
        SpaceCadetPinball/TDemo.h
        SpaceCadetPinball/TDrain.cpp
        SpaceCadetPinball/TDrain.h
        SpaceCadetPinball/TEdgeBox.h
        SpaceCadetPinball/TEdgeManager.cpp
        SpaceCadetPinball/TEdgeManager.h
        SpaceCadetPinball/TEdgeSegment.cpp
        SpaceCadetPinball/TEdgeSegment.h
        SpaceCadetPinball/TFlagSpinner.cpp
        SpaceCadetPinball/TFlagSpinner.h
        SpaceCadetPinball/TFlipper.cpp
        SpaceCadetPinball/TFlipper.h
        SpaceCadetPinball/TFlipperEdge.cpp
        SpaceCadetPinball/TFlipperEdge.h
        SpaceCadetPinball/TGate.cpp
        SpaceCadetPinball/TGate.h
        SpaceCadetPinball/THole.cpp
        SpaceCadetPinball/THole.h
        SpaceCadetPinball/timer.cpp
        SpaceCadetPinball/timer.h
        SpaceCadetPinball/TKickback.cpp
        SpaceCadetPinball/TKickback.h
        SpaceCadetPinball/TKickout.cpp
        SpaceCadetPinball/TKickout.h
        SpaceCadetPinball/TLight.cpp
        SpaceCadetPinball/TLight.h
        SpaceCadetPinball/TLightBargraph.cpp
        SpaceCadetPinball/TLightBargraph.h
        SpaceCadetPinball/TLightGroup.cpp
        SpaceCadetPinball/TLightGroup.h
        SpaceCadetPinball/TLightRollover.cpp
        SpaceCadetPinball/TLightRollover.h
        SpaceCadetPinball/TLine.cpp
        SpaceCadetPinball/TLine.h
        SpaceCadetPinball/TOneway.cpp
        SpaceCadetPinball/TOneway.h
        SpaceCadetPinball/TPinballComponent.cpp
        SpaceCadetPinball/TPinballComponent.h
        SpaceCadetPinball/TPinballTable.cpp
        SpaceCadetPinball/TPinballTable.h
        SpaceCadetPinball/TPlunger.cpp
        SpaceCadetPinball/TPlunger.h
        SpaceCadetPinball/TPopupTarget.cpp
        SpaceCadetPinball/TPopupTarget.h
        SpaceCadetPinball/TRamp.cpp
        SpaceCadetPinball/TRamp.h
        SpaceCadetPinball/translations.cpp
        SpaceCadetPinball/translations.h
        SpaceCadetPinball/TRollover.cpp
        SpaceCadetPinball/TRollover.h
        SpaceCadetPinball/TSink.cpp
        SpaceCadetPinball/TSink.h
        SpaceCadetPinball/TSoloTarget.cpp
        SpaceCadetPinball/TSoloTarget.h
        SpaceCadetPinball/TSound.cpp
        SpaceCadetPinball/TSound.h
        SpaceCadetPinball/TTableLayer.cpp
        SpaceCadetPinball/TTableLayer.h
        SpaceCadetPinball/TTextBox.cpp
        SpaceCadetPinball/TTextBox.h
        SpaceCadetPinball/TTextBoxMessage.cpp
        SpaceCadetPinball/TTextBoxMessage.h
        SpaceCadetPinball/TTimer.cpp
        SpaceCadetPinball/TTimer.h
        SpaceCadetPinball/TTripwire.cpp
        SpaceCadetPinball/TTripwire.h
        SpaceCadetPinball/TWall.cpp
        SpaceCadetPinball/TWall.h
        SpaceCadetPinball/pinball_platform.cpp
        SpaceCadetPinball/pinball_platform.h
        SpaceCadetPinball/Help.cpp
        SpaceCadetPinball/Help.h
        SpaceCadetPinball/winmain.cpp
        SpaceCadetPinball/winmain.h
        SpaceCadetPinball/zdrv.cpp
        SpaceCadetPinball/zdrv.h
        SpaceCadetPinball/imconfig.h
        SpaceCadetPinball/imgui_internal.h
        SpaceCadetPinball/imgui.cpp
        SpaceCadetPinball/imgui.h
        SpaceCadetPinball/imgui_sdl.cpp
        SpaceCadetPinball/imgui_sdl.h
        SpaceCadetPinball/imgui_draw.cpp
        SpaceCadetPinball/imgui_widgets.cpp
        SpaceCadetPinball/imgui_tables.cpp
        SpaceCadetPinball/imgui_demo.cpp
        SpaceCadetPinball/imgui_impl_sdl.cpp
        SpaceCadetPinball/imgui_impl_sdl.h
        SpaceCadetPinball/imgui_impl_sdlrenderer.h
        SpaceCadetPinball/imgui_impl_sdlrenderer.cpp
        SpaceCadetPinball/imstb_textedit.h
        SpaceCadetPinball/imstb_rectpack.h
        SpaceCadetPinball/imstb_truetype.h
        SpaceCadetPinball/DebugOverlay.cpp
        SpaceCadetPinball/DebugOverlay.h
        )


list(REMOVE_ITEM SOURCE_FILES SpaceCadetPinball/SpaceCadetPinball.cpp)
add_library(pinball_core STATIC ${SOURCE_FILES})
set_target_properties(pinball_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
set(ENTRY_FILES SpaceCadetPinball/SpaceCadetPinball.cpp)
if(WIN32)
    enable_language(RC)
    list(APPEND ENTRY_FILES SpaceCadetPinball/SpaceCadetPinball.rc)
endif()
if(ANDROID)
    add_library(main SHARED ${ENTRY_FILES})
    set(PINBALL_TARGET main)
else()
    add_executable(SpaceCadetPinball ${ENTRY_FILES})
    set(PINBALL_TARGET SpaceCadetPinball)
endif()
target_link_libraries(${PINBALL_TARGET} PRIVATE pinball_core)
if(EMSCRIPTEN)
    target_compile_options(pinball_core PUBLIC -fexceptions "-sUSE_SDL=2" "-sUSE_SDL_MIXER=2" "-sSDL2_MIXER_FORMATS=[]")
    target_link_options(${PINBALL_TARGET} PRIVATE -fexceptions "-sUSE_SDL=2" "-sUSE_SDL_MIXER=2" "-sSDL2_MIXER_FORMATS=[]"
        "-sALLOW_MEMORY_GROWTH=1" "-sFORCE_FILESYSTEM=1" "-sDYNAMIC_EXECUTION=0"
        "-sEXPORTED_FUNCTIONS=['_main','_platform_notify_pause','_platform_reset_input_session','_platform_cancel_inputs','_platform_flush_settings','_platform_activate_audio','_platform_is_english','_platform_web_name_edit','_platform_web_name_submit']"
        "-sEXPORTED_RUNTIME_METHODS=['FS','addRunDependency','removeRunDependency','ccall']"
        "-lidbfs.js" "--preload-file" "${PINBALL_ASSETS_DIR}@/assets")
    set_target_properties(${PINBALL_TARGET} PROPERTIES SUFFIX ".js")
else()
    target_link_libraries(pinball_core PUBLIC SDL2::SDL2 SDL2_mixer::SDL2_mixer)
endif()
if(MINGW)
    target_link_options(${PINBALL_TARGET} PRIVATE -static)
endif()
if(WIN32)
    add_custom_command(TARGET ${PINBALL_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:${PINBALL_TARGET}>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL2_mixer::SDL2_mixer> $<TARGET_FILE_DIR:${PINBALL_TARGET}>
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${PINBALL_ASSETS_DIR}" "$<TARGET_FILE_DIR:${PINBALL_TARGET}>/assets")
endif()
set_source_files_properties(
    SpaceCadetPinball/imgui.cpp SpaceCadetPinball/imgui_sdl.cpp SpaceCadetPinball/imgui_draw.cpp
    SpaceCadetPinball/imgui_widgets.cpp SpaceCadetPinball/imgui_tables.cpp SpaceCadetPinball/imgui_demo.cpp
    SpaceCadetPinball/imgui_impl_sdl.cpp SpaceCadetPinball/imgui_impl_sdlrenderer.cpp
    PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
target_precompile_headers(pinball_core PRIVATE SpaceCadetPinball/pch.h)

option(PINBALL_BUILD_TESTS "Build real-core runtime checks" OFF)
if(PINBALL_BUILD_TESTS AND NOT ANDROID AND NOT EMSCRIPTEN)
    enable_testing()
    add_executable(pinball_runtime_checks tests/runtime_checks.cpp SpaceCadetPinball/SpaceCadetPinball.cpp)
    target_compile_definitions(pinball_runtime_checks PRIVATE PINBALL_TEST_MAIN)
    target_include_directories(pinball_runtime_checks PRIVATE SpaceCadetPinball)
    target_link_libraries(pinball_runtime_checks PRIVATE pinball_core)
    if(MINGW)
        target_link_options(pinball_runtime_checks PRIVATE -static)
    endif()
    add_test(NAME runtime_boundaries COMMAND pinball_runtime_checks "${PINBALL_ASSETS_DIR}")
endif()
