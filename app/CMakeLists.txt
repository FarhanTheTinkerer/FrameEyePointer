set(APP_SOURCES
    src/main.cpp
    src/EyePointerApp.cpp
    src/Log.cpp
)
if(WIN32)
    list(APPEND APP_SOURCES src/Platform_win.cpp)
else()
    list(APPEND APP_SOURCES src/Platform_posix.cpp)
endif()

add_executable(FrameEyePointer ${APP_SOURCES})
target_link_libraries(FrameEyePointer PRIVATE eyepointer_core openvr_api)
target_compile_definitions(FrameEyePointer PRIVATE
    FRAMEEYEPOINTER_VERSION="${PROJECT_VERSION}")

if(WIN32)
    # No console window when SteamVR launches it; --console attaches one.
    set_target_properties(FrameEyePointer PROPERTIES WIN32_EXECUTABLE TRUE)
    if(MSVC)
        target_link_options(FrameEyePointer PRIVATE /ENTRY:mainCRTStartup)
        set_property(TARGET FrameEyePointer PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    elseif(MINGW)
        target_link_options(FrameEyePointer PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
else()
    set_target_properties(FrameEyePointer PROPERTIES
        BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
endif()

# Everything SteamVR needs sits next to the executable.
set(APP_RESOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/actions.json
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/frameeyepointer.vrmanifest
)
file(GLOB APP_BINDINGS ${CMAKE_CURRENT_SOURCE_DIR}/resources/bindings/*.json)

add_custom_command(TARGET FrameEyePointer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OPENVR_RUNTIME} $<TARGET_FILE_DIR:FrameEyePointer>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${APP_RESOURCES} $<TARGET_FILE_DIR:FrameEyePointer>
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:FrameEyePointer>/bindings
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${APP_BINDINGS} $<TARGET_FILE_DIR:FrameEyePointer>/bindings
)

install(TARGETS FrameEyePointer RUNTIME DESTINATION .)
install(FILES ${OPENVR_RUNTIME} ${APP_RESOURCES} DESTINATION .)
install(FILES ${APP_BINDINGS} DESTINATION bindings)
install(FILES ${PROJECT_SOURCE_DIR}/README.md DESTINATION .)
install(FILES ${PROJECT_SOURCE_DIR}/third_party/openvr/LICENSE
        DESTINATION . RENAME OpenVR-LICENSE.txt)
