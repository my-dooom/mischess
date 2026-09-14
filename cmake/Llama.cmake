# Builds llama.cpp from the vendored submodule and links it into the game so
# the strategist can run a local GGUF language model. The model file itself
# is not part of the build: drop one into models/ next to the executable.
#
# Options:
#   MISCHESS_BUILD_LLM   ON/OFF (default ON when the submodule is present)
#   MISCHESS_LLM_NATIVE  ON tunes ggml for this CPU (fastest, not portable);
#                        OFF (default) builds a generic x86-64 + AVX2 binary

set(LLAMA_SOURCE_DIR ${CMAKE_SOURCE_DIR}/llama.cpp)

if(NOT EXISTS ${LLAMA_SOURCE_DIR}/CMakeLists.txt)
    message(STATUS "llama.cpp submodule not checked out, strategist disabled "
                   "(run: git submodule update --init llama.cpp)")
    return()
endif()

option(MISCHESS_BUILD_LLM "Build llama.cpp for the strategist (local LLM)" ON)
option(MISCHESS_LLM_NATIVE "Tune llama.cpp for this CPU (not portable)" OFF)

if(NOT MISCHESS_BUILD_LLM)
    return()
endif()

# llama.cpp is C++; the rest of the project is C
enable_language(CXX)
set(CMAKE_CXX_STANDARD 17)

# keep it lean and static: no examples, tools, tests or shared libraries
set(BUILD_SHARED_LIBS      OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_COMMON     OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TESTS      OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TOOLS      OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES   OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER     OFF CACHE BOOL "" FORCE)
set(LLAMA_CURL             OFF CACHE BOOL "" FORCE)
set(GGML_OPENMP            OFF CACHE BOOL "" FORCE) # avoids a libgomp DLL
set(GGML_NATIVE ${MISCHESS_LLM_NATIVE} CACHE BOOL "" FORCE)
if(NOT MISCHESS_LLM_NATIVE AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(GGML_AVX2 ON CACHE BOOL "" FORCE)
    set(GGML_FMA  ON CACHE BOOL "" FORCE)
    set(GGML_F16C ON CACHE BOOL "" FORCE)
endif()

add_subdirectory(${LLAMA_SOURCE_DIR} EXCLUDE_FROM_ALL)

target_link_libraries(${PROJECT_NAME} PRIVATE llama)
target_compile_definitions(${PROJECT_NAME} PRIVATE MISCHESS_HAVE_LLM)
if(MINGW)
    target_link_options(${PROJECT_NAME} PRIVATE -static-libstdc++)
endif()

message(STATUS "Strategist enabled: llama.cpp built from source "
               "(native=${MISCHESS_LLM_NATIVE})")
