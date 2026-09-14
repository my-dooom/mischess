cmake_minimum_required(VERSION 3.16)
project(mischess C)

set(CMAKE_C_STANDARD 99)

# MSVC flags every sscanf/strcpy as deprecated; they are used safely here
if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# ------------------------------------------------------------
# Portable executable: the C runtime is linked statically so the .exe has
# no MinGW/MSVC DLL dependencies (must be set before any target is created)
# ------------------------------------------------------------
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Build raylib from source (vendored); disable unused features for faster builds
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
add_subdirectory(raylib EXCLUDE_FROM_ALL)

# ------------------------------------------------------------
# Assets are compiled into the executable, so nothing has to sit beside it
# ------------------------------------------------------------
include(cmake/EmbedFile.cmake)
set(GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
embed_file(${CMAKE_SOURCE_DIR}/assets/atlas.png atlas_png
           ${GENERATED_DIR}/atlas_png.c ${GENERATED_DIR}/atlas_png.h)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             ${CMAKE_SOURCE_DIR}/assets/atlas.png)

file(GLOB_RECURSE SOURCES src/*.c)
add_executable(${PROJECT_NAME} ${SOURCES} ${GENERATED_DIR}/atlas_png.c)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/raylib/src
    ${CMAKE_SOURCE_DIR}/src/include
    ${GENERATED_DIR}
)

# raylib's own CMake target carries all platform-specific link dependencies
target_link_libraries(${PROJECT_NAME} PRIVATE raylib)

if(MINGW)
    target_link_options(${PROJECT_NAME} PRIVATE -static -static-libgcc)
endif()

# Hide the console window in release builds (MSVC then expects WinMain
# unless told to keep the plain main entry point)
set_target_properties(${PROJECT_NAME} PROPERTIES
    WIN32_EXECUTABLE $<CONFIG:Release>
)
if(MSVC)
    target_link_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:/ENTRY:mainCRTStartup>)
endif()

# ------------------------------------------------------------
# Stockfish (coach engine), built from the vendored submodule
# ------------------------------------------------------------
include(cmake/Stockfish.cmake)

# ------------------------------------------------------------
# Installation rules (used by CPack)
# ------------------------------------------------------------
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

# ------------------------------------------------------------
# CPack — generates platform-native installers
# ------------------------------------------------------------
set(CPACK_PACKAGE_NAME        "mischess")
set(CPACK_PACKAGE_VENDOR      "miszc")
set(CPACK_PACKAGE_VERSION     "1.0.0")
set(CPACK_PACKAGE_DESCRIPTION "A chess game built with raylib")
set(CPACK_PACKAGE_EXECUTABLES "${PROJECT_NAME};mischess")

set(CPACK_GENERATOR "ZIP")

include(CPack)

# ------------------------------------------------------------
# Test target (no raylib, no window -- pure logic)
# ------------------------------------------------------------
enable_testing()

set(TEST_SOURCES
    tests/test_rules.c
    src/board.c
    src/game.c
    src/fen.c
    src/notation.c
    src/uci.c
)

add_executable(chess_tests ${TEST_SOURCES})

target_include_directories(chess_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/tests   # raylib.h stub lives here, shadows the real one
    ${CMAKE_SOURCE_DIR}/src/include
)

# tests/raylib_stub.h shadows the real raylib.h so no library is needed
add_test(NAME chess_rules COMMAND chess_tests)
