# Steam Frame (on-headset) build: the SteamVR driver and the service.
# The Frame runs SteamOS on ARM64 with glibc 2.39; build with an aarch64 toolchain
# whose glibc is no newer (cmake/aarch64-linux-gnu.cmake on Ubuntu 24.04 does).

if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    set(STEAMVR_PLATFORM linuxarm64)
else()
    set(STEAMVR_PLATFORM linux64)
endif()

# SteamVR driver, loaded into vrserver: no C++ runtime of its own leaks out.
add_library(driver_frameeyepointer SHARED driver/driver_frameeyepointer.cpp)
set_target_properties(driver_frameeyepointer PROPERTIES PREFIX "" CXX_VISIBILITY_PRESET hidden)
target_include_directories(driver_frameeyepointer PRIVATE ${PROJECT_SOURCE_DIR}/third_party/openvr/headers)
target_compile_options(driver_frameeyepointer PRIVATE -fno-math-errno -Wno-unused-parameter)
target_link_options(driver_frameeyepointer PRIVATE -static-libstdc++ -static-libgcc -Wl,--exclude-libs,ALL)
target_link_libraries(driver_frameeyepointer PRIVATE pthread)

add_executable(frameeyepointerd
    service/frameeyepointerd.cpp
    service/EyeGazeSource.cpp
    service/ControllerButtonSource.cpp
    service/ExternalInput.cpp
)
target_link_libraries(frameeyepointerd PRIVATE eyepointer_core openvr_api pthread)
target_compile_definitions(frameeyepointerd PRIVATE FRAMEEYEPOINTER_VERSION="${PROJECT_VERSION}")
target_link_options(frameeyepointerd PRIVATE -static-libstdc++ -static-libgcc)
set_target_properties(frameeyepointerd PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")

add_custom_command(TARGET frameeyepointerd POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OPENVR_RUNTIME} $<TARGET_FILE_DIR:frameeyepointerd>
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/service/actions
            $<TARGET_FILE_DIR:frameeyepointerd>/actions)

# Package layout (cmake --install build --prefix <dir>): everything install.sh needs.
install(TARGETS frameeyepointerd RUNTIME DESTINATION .)
install(FILES ${OPENVR_RUNTIME} DESTINATION .)
install(DIRECTORY service/actions DESTINATION .)
install(DIRECTORY driver/frameeyepointer DESTINATION driver)
install(TARGETS driver_frameeyepointer LIBRARY DESTINATION driver/frameeyepointer/bin/${STEAMVR_PLATFORM})
install(PROGRAMS install.sh tools/frameeyepointer-send DESTINATION .)
install(FILES frameeyepointer.service DESTINATION .)
install(FILES ${PROJECT_SOURCE_DIR}/third_party/openvr/LICENSE DESTINATION . RENAME OpenVR-LICENSE.txt)
install(FILES ${PROJECT_SOURCE_DIR}/THIRD_PARTY.md ${PROJECT_SOURCE_DIR}/docs/STEAM_FRAME.md
              ${PROJECT_SOURCE_DIR}/docs/INPUT_SOURCES.md DESTINATION .)
