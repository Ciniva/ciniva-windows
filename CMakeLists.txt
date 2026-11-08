cmake_minimum_required(VERSION 3.25)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" CINIVA_VERSION)
string(STRIP "${CINIVA_VERSION}" CINIVA_VERSION)

project(Ciniva VERSION ${CINIVA_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(cmake/Deps.cmake)

add_library(ciniva_core STATIC
    src/core/client.cpp
    src/core/credentials.cpp
    src/core/http.cpp
    src/core/models.cpp
    src/core/settings.cpp
    src/core/sse.cpp
    src/core/text.cpp
    src/core/url.cpp
)
target_include_directories(ciniva_core PUBLIC src ${JSON_INCLUDE_DIR})
target_link_libraries(ciniva_core PUBLIC winhttp crypt32 advapi32)
target_compile_definitions(ciniva_core PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(ciniva_core PUBLIC /W4 /permissive- /utf-8)

configure_file(src/version.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/version.h" @ONLY)

add_executable(ciniva WIN32
    src/main.cpp
    src/app/app_model.cpp
    src/app/dispatch.cpp
    src/app/main_window.cpp
    src/app/overlay_window.cpp
    src/app/renderer.cpp
    src/app/timing.cpp
    src/app/video_host.cpp
    src/screens/components.cpp
    src/screens/login_screen.cpp
    src/screens/pages.cpp
    src/screens/shell_screen.cpp
    src/ui/images.cpp
    src/ui/text_field.cpp
    src/ui/ui.cpp
    src/ui/widgets.cpp
    src/mpv/mpv_api.cpp
    src/mpv/player.cpp
    src/player/player_screen.cpp
    res/ciniva.rc
    res/ciniva.manifest
)
target_include_directories(ciniva PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated" ${MPV_INCLUDE_DIR})
target_link_libraries(ciniva PRIVATE ciniva_core d3d11 dxgi d2d1 dwrite windowscodecs dwmapi comctl32 dcomp)
set_target_properties(ciniva PROPERTIES OUTPUT_NAME ciniva)
add_custom_command(TARGET ciniva POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MPV_DLL}" "$<TARGET_FILE_DIR:ciniva>"
)

enable_testing()
add_executable(ciniva_tests tests/tests.cpp)
target_link_libraries(ciniva_tests PRIVATE ciniva_core)
add_test(NAME ciniva_tests COMMAND ciniva_tests)
