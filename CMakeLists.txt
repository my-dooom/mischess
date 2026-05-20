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

if(WIN32)
    # Hide the console window in release builds
    set_target_properties(${PROJECT_NAME} PROPERTIES
        WIN32_EXECUTABLE $<CONFIG:Release>
    )
endif()

if(APPLE)
    # For macOS .app bundles GLFW's COCOA_CHDIR_RESOURCES switches the working
    # directory to Contents/Resources at launch, so assets must live there.
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets
            $<TARGET_BUNDLE_CONTENT_DIR:${PROJECT_NAME}>/Resources/assets
        COMMENT "Copying assets into .app bundle Resources"
    )
else()
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets
        COMMENT "Copying assets to build directory"
    )
endif()

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

# Windows: NSIS installer (requires NSIS installed on the build machine)
if(WIN32)
    set(CPACK_GENERATOR "ZIP")
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "mischess")
    # Package the executable as a proper .app bundle
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME        "mischess"
        MACOSX_BUNDLE_BUNDLE_VERSION     "1.0.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0.0"
        MACOSX_BUNDLE_GUI_IDENTIFIER     "com.miszc.mischess"
    )
    # Install as a .app bundle instead of a bare binary
    install(TARGETS ${PROJECT_NAME} BUNDLE DESTINATION .)
    # Bundle the assets inside Contents/Resources so they are found at runtime
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/
        DESTINATION mischess.app/Contents/Resources/assets
    )
else()
    # Linux: prefer DEB, fall back to RPM and TGZ
    set(CPACK_GENERATOR "DEB;RPM;TGZ")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "miszc")
    set(CPACK_DEBIAN_PACKAGE_SECTION    "games")
    set(CPACK_RPM_PACKAGE_LICENSE       "Proprietary")
endif()

include(CPack)
