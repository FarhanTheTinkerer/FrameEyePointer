cmake_minimum_required(VERSION 3.20)
project(FrameEyePointer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(EYEPOINTER_BUILD_APP "Build the SteamVR overlay application" ON)
option(EYEPOINTER_BUILD_TESTS "Build unit tests for the core library" ON)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# eyepointer_core: platform-independent gaze filtering, desktop mapping and
# pointer logic. No OpenVR dependency, so other apps (e.g. SlopCadVR) can reuse it.
# ---------------------------------------------------------------------------
add_library(eyepointer_core STATIC
    core/src/DesktopMapping.cpp
    core/src/GazeFilter.cpp
    core/src/GazeTracker.cpp
    core/src/PointerController.cpp
    core/src/Reticle.cpp
    core/src/Settings.cpp
)
target_include_directories(eyepointer_core PUBLIC core/include)

if(EYEPOINTER_BUILD_APP)
    # OpenVR SDK 2.15.6, vendored under third_party/openvr.
    set(OPENVR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/openvr)
    add_library(openvr_api SHARED IMPORTED GLOBAL)
    target_include_directories(openvr_api INTERFACE ${OPENVR_DIR}/headers)
    if(WIN32)
        set(OPENVR_RUNTIME ${OPENVR_DIR}/bin/win64/openvr_api.dll)
        if(MSVC)
            set_target_properties(openvr_api PROPERTIES
                IMPORTED_LOCATION ${OPENVR_RUNTIME}
                IMPORTED_IMPLIB ${OPENVR_DIR}/lib/win64/openvr_api.lib)
        else()
            # MinGW links directly against the DLL.
            set_target_properties(openvr_api PROPERTIES
                IMPORTED_LOCATION ${OPENVR_RUNTIME}
                IMPORTED_IMPLIB ${OPENVR_RUNTIME})
        endif()
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
        set(OPENVR_RUNTIME ${OPENVR_DIR}/bin/linuxarm64/libopenvr_api.so)
        set_target_properties(openvr_api PROPERTIES IMPORTED_LOCATION ${OPENVR_RUNTIME})
    else()
        set(OPENVR_RUNTIME ${OPENVR_DIR}/bin/linux64/libopenvr_api.so)
        set_target_properties(openvr_api PROPERTIES IMPORTED_LOCATION ${OPENVR_RUNTIME})
    endif()

    add_subdirectory(app)
endif()

if(EYEPOINTER_BUILD_TESTS)
    enable_testing()
    add_executable(eyepointer_tests
        tests/main.cpp
        tests/test_desktop.cpp
        tests/test_gaze.cpp
        tests/test_settings.cpp
    )
    target_link_libraries(eyepointer_tests PRIVATE eyepointer_core)
    add_test(NAME eyepointer_tests COMMAND eyepointer_tests)
endif()
