cmake_minimum_required(VERSION 3.22.1)
project(xrwrist)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# OpenXR headers (Khronos SDK, downloaded into third_party/)
set(OPENXR_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/openxr/include)

# OpenXR loader: link at build time (also packaged via the Gradle AAR dep).
add_library(openxr_loader SHARED IMPORTED)
set_target_properties(openxr_loader PROPERTIES
    IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/third_party/openxr/lib/arm64-v8a/libopenxr_loader.so)

add_library(xrwrist SHARED
    main.cpp
    xr_core.cpp
    gl_render.cpp
    video.cpp
    net.cpp
    input.cpp
    avatar.cpp
    devtools.cpp
    voice.cpp
    config.cpp
    notify.cpp
    environment.cpp
    menu.cpp
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
)

target_include_directories(xrwrist PRIVATE
    ${OPENXR_INCLUDE_DIR}
    ${ANDROID_NDK}/sources/android/native_app_glue
)

# The OpenXR loader .so ships inside the openxr_loader_for_android AAR and is
# dlopen()'d at runtime (see xr_core.cpp), so we do NOT link it here.
find_library(android-lib android)
find_library(log-lib log)
find_library(egl-lib EGL)
find_library(gles-lib GLESv3)
find_library(mediandk-lib mediandk)

target_link_libraries(xrwrist
    openxr_loader
    ${android-lib}
    ${log-lib}
    ${egl-lib}
    ${gles-lib}
    ${mediandk-lib}
)

target_compile_options(xrwrist PRIVATE -Wall -Wextra -Wno-unused-parameter)

# Release optimization: -O3 for release builds, -O0 -g for debug.
target_compile_options(xrwrist PRIVATE
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:RelWithDebInfo>:-O2>
    $<$<CONFIG:Debug>:-O0 -g>
)
