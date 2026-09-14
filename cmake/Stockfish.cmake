# Builds Stockfish from the vendored submodule with its own Makefile and drops
# the binary next to the game as engines/stockfish[.exe], where the coach
# looks for it. Nothing here touches the submodule checkout: the sources are
# copied into the build tree first.
#
# Options:
#   MISCHESS_BUILD_STOCKFISH  ON/OFF   (default ON when the submodule is present)
#   MISCHESS_STOCKFISH_ARCH   Stockfish ARCH value, "native" picks the best
#                             instruction set for this machine; use a portable
#                             one such as x86-64-avx2 for binaries you ship.
#
# Requirements: a C++17 compiler (g++/clang++), GNU make and a POSIX shell.
# On Windows that means MSYS2's make (C:/msys64/usr/bin/make.exe), which
# CMake >= 3.25 is needed to put on PATH for the build step.

set(SF_SOURCE_DIR ${CMAKE_SOURCE_DIR}/stockfish)

if(NOT EXISTS ${SF_SOURCE_DIR}/src/Makefile)
    message(STATUS "Stockfish submodule not checked out, coach engine not built "
                   "(run: git submodule update --init stockfish)")
    return()
endif()

option(MISCHESS_BUILD_STOCKFISH
       "Build Stockfish from the submodule and bundle it as the coach engine" ON)
set(MISCHESS_STOCKFISH_ARCH "native" CACHE STRING
    "Stockfish ARCH (native, x86-64-avx2, x86-64, apple-silicon, ...)")

if(NOT MISCHESS_BUILD_STOCKFISH)
    return()
endif()

# --- tools --------------------------------------------------------------------
if(WIN32)
    # GnuWin32/mingw32-make cannot run the Makefile; it needs the MSYS one
    find_program(SF_MAKE NAMES make
                 PATHS C:/msys64/usr/bin C:/msys32/usr/bin
                 NO_DEFAULT_PATH)
    find_program(SF_MAKE NAMES make)
    set(SF_COMP mingw)
else()
    find_program(SF_MAKE NAMES gmake make)
    if(APPLE)
        set(SF_COMP clang)
    else()
        set(SF_COMP gcc)
    endif()
endif()
find_program(SF_CXX NAMES g++ x86_64-w64-mingw32-g++ clang++ c++)

if(NOT SF_MAKE OR NOT SF_CXX)
    message(WARNING "Stockfish not built: need GNU make and a C++ compiler "
                    "(make='${SF_MAKE}', c++='${SF_CXX}'). "
                    "Set -DMISCHESS_BUILD_STOCKFISH=OFF to silence this.")
    return()
endif()

include(ProcessorCount)
ProcessorCount(SF_JOBS)
if(SF_JOBS EQUAL 0)
    set(SF_JOBS 2)
endif()

# put make and the compiler on PATH for the build step (Windows: MSYS make
# also needs its own /usr/bin for sh, curl, ...)
get_filename_component(SF_MAKE_DIR ${SF_MAKE} DIRECTORY)
get_filename_component(SF_CXX_DIR ${SF_CXX} DIRECTORY)
if(WIN32)
    if(CMAKE_VERSION VERSION_LESS 3.25)
        message(WARNING "Stockfish not built: CMake >= 3.25 is needed on Windows")
        return()
    endif()
    set(SF_ENV ${CMAKE_COMMAND} -E env
        --modify PATH=path_list_prepend:${SF_MAKE_DIR}
        --modify PATH=path_list_prepend:${SF_CXX_DIR})
else()
    set(SF_ENV ${CMAKE_COMMAND} -E env)
endif()

# --- build --------------------------------------------------------------------
set(SF_BUILD_DIR ${CMAKE_BINARY_DIR}/stockfish)
set(SF_EXE ${SF_BUILD_DIR}/src/stockfish${CMAKE_EXECUTABLE_SUFFIX})
file(GLOB_RECURSE SF_SOURCES CONFIGURE_DEPENDS
     ${SF_SOURCE_DIR}/src/*.cpp ${SF_SOURCE_DIR}/src/*.h
     ${SF_SOURCE_DIR}/src/Makefile ${SF_SOURCE_DIR}/scripts/*)

add_custom_command(
    OUTPUT ${SF_EXE}
    # the Makefile builds in-tree and needs ../scripts next to src
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${SF_SOURCE_DIR}/src ${SF_BUILD_DIR}/src
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${SF_SOURCE_DIR}/scripts ${SF_BUILD_DIR}/scripts
    # "build" also fetches the NNUE network files the engine embeds
    COMMAND ${SF_ENV} ${SF_MAKE} -j${SF_JOBS} build
            ARCH=${MISCHESS_STOCKFISH_ARCH} COMP=${SF_COMP}
    WORKING_DIRECTORY ${SF_BUILD_DIR}/src
    DEPENDS ${SF_SOURCES}
    COMMENT "Building Stockfish (${MISCHESS_STOCKFISH_ARCH}) from source"
    VERBATIM)

add_custom_target(stockfish ALL DEPENDS ${SF_EXE})

# bundle it where coach_init() looks: <exe dir>/engines/stockfish[.exe]
add_dependencies(${PROJECT_NAME} stockfish)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${PROJECT_NAME}>/engines
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SF_EXE}
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/engines/
    COMMENT "Bundling Stockfish into engines/")

install(PROGRAMS ${SF_EXE} DESTINATION bin/engines)

message(STATUS "Stockfish will be built from source (ARCH=${MISCHESS_STOCKFISH_ARCH}, "
               "make=${SF_MAKE}, c++=${SF_CXX})")
