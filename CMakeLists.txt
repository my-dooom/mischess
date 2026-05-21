cmake_minimum_required(VERSION 3.16)
project(mischess C)

set(CMAKE_C_STANDARD 99)

# Build raylib from source (vendored); disable unused features for faster builds
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
add_subdirectory(raylib EXCLUDE_FROM_ALL)

file(GLOB_RECURSE SOURCES src/*.c)
add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/raylib/src
    ${CMAKE_SOURCE_DIR}/src/include
)

# raylib's own CMake target carries all platform-specific link dependencies
target_link_libraries(${PROJECT_NAME} PRIVATE raylib)

# Hide the console window in release builds
set_target_properties(${PROJECT_NAME} PROPERTIES
    WIN32_EXECUTABLE $<CONFIG:Release>
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets
    COMMENT "Copying assets to build directory"
)

# ------------------------------------------------------------
# Installation rules (used by CPack)
# ------------------------------------------------------------
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/
    DESTINATION bin/assets
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
