cmake_minimum_required(VERSION 3.25)

project(SonicPocket LANGUAGES C CXX)

include(FetchContent)

set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.8
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(SDL3)

add_executable(sonic-pocket-viewer
    src/viewer/main.cpp
)

target_compile_features(sonic-pocket-viewer PRIVATE cxx_std_20)
target_link_libraries(sonic-pocket-viewer PRIVATE SDL3::SDL3)

if(MSVC)
    target_compile_options(sonic-pocket-viewer PRIVATE /W4 /permissive-)
else()
    target_compile_options(sonic-pocket-viewer PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_custom_command(
    TARGET sonic-pocket-viewer
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL3::SDL3>
        $<TARGET_FILE_DIR:sonic-pocket-viewer>
)

# ---------------------------------------------------------------------------
# Recomp runtime: NGPC machine (MAME-derived CPU/video, HLE BIOS) and tools.

add_library(tlcs900 STATIC
    src/cpu/tlcs900/tlcs900.cpp
)
target_include_directories(tlcs900 PUBLIC src)
target_compile_features(tlcs900 PUBLIC cxx_std_20)

add_library(z80core STATIC
    third_party/z80/z80.c
)
target_include_directories(z80core PUBLIC third_party/z80)

add_library(ngpc STATIC
    src/ngpc/ngpc_cpu.cpp
    src/ngpc/k2ge.cpp
    src/ngpc/machine.cpp
    src/common/png.cpp
)
target_include_directories(ngpc PUBLIC src)
target_link_libraries(ngpc PUBLIC tlcs900 z80core)

add_executable(ngpc-run src/apps/ngpc_run.cpp)
target_link_libraries(ngpc-run PRIVATE ngpc)

if(MSVC)
    target_compile_options(tlcs900 PRIVATE /W3)
    target_compile_options(ngpc PRIVATE /W4 /permissive-)
    target_compile_options(ngpc-run PRIVATE /W4 /permissive-)
endif()

# ---------------------------------------------------------------------------
# Static recompiler and the recompiled game.

add_executable(spa-recomp
    src/recomp/main.cpp
    src/recomp/decoder.cpp
)
target_link_libraries(spa-recomp PRIVATE tlcs900)

set(SPA_ROM "" CACHE FILEPATH "Sonic Pocket Adventure cartridge image used to generate the recompiled code")
set(SPA_LISTING "" CACHE FILEPATH "Optional listing_instructions.txt for disassembly comments in generated code")

if(SPA_ROM)
    set(SPA_GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
    set(SPA_GENERATED_SOURCES ${SPA_GENERATED_DIR}/dispatch.cpp)
    foreach(shard RANGE 0 31)
        string(LENGTH "${shard}" shard_length)
        if(shard_length EQUAL 1)
            set(shard "0${shard}")
        endif()
        list(APPEND SPA_GENERATED_SOURCES ${SPA_GENERATED_DIR}/game_${shard}.cpp)
    endforeach()

    set(SPA_RECOMP_ARGS
        --rom ${SPA_ROM}
        --functions ${CMAKE_SOURCE_DIR}/config/recomp/functions.txt
        --out ${SPA_GENERATED_DIR}
    )
    if(SPA_LISTING)
        list(APPEND SPA_RECOMP_ARGS --listing ${SPA_LISTING})
    endif()

    add_custom_command(
        OUTPUT ${SPA_GENERATED_SOURCES} ${SPA_GENERATED_DIR}/functions.h
        COMMAND spa-recomp ${SPA_RECOMP_ARGS}
        DEPENDS spa-recomp ${CMAKE_SOURCE_DIR}/config/recomp/functions.txt ${SPA_ROM}
        COMMENT "Recompiling Sonic Pocket Adventure"
        VERBATIM
    )

    add_library(spa_game STATIC
        ${SPA_GENERATED_SOURCES}
        src/recomp_rt/runtime.cpp
        src/recomp_rt/game.cpp
    )
    target_include_directories(spa_game PUBLIC src ${SPA_GENERATED_DIR})
    target_link_libraries(spa_game PUBLIC ngpc)
    if(MSVC)
        target_compile_options(spa_game PRIVATE /bigobj /EHsc)
    endif()

    add_executable(spa-verify src/apps/spa_verify.cpp)
    target_link_libraries(spa-verify PRIVATE spa_game)
endif()
