cmake_minimum_required(VERSION 3.16)
project(orbita_r36s VERSION 0.2.3 LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2>=2.0.10)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl>=7.58)
pkg_check_modules(AV REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale)
pkg_check_modules(OPUS REQUIRED IMPORTED_TARGET opus)
set(ORBITA_WEBRTC_ROOT "$ENV{ORBITA_WEBRTC_ROOT}" CACHE PATH "Prefix from deps/webrtc-linux/build.sh (host directory)")
if(NOT EXISTS "${ORBITA_WEBRTC_ROOT}/lib/libpeer-xc.a")
    message(FATAL_ERROR "Build deps/webrtc-linux/build.sh, then set -DORBITA_WEBRTC_ROOT=<output>/host")
endif()
option(ORBITA_BUILD_VIDEO_TEST "Build the H.264 device benchmark" ON)
include(CTest)

get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(CORE "${REPO}/app/h264play/source")
set(LINUX "${REPO}/platform/linux")
add_library(orbita_linux STATIC "${LINUX}/xc_port_linux.c" "${LINUX}/https_curl.c")
target_include_directories(orbita_linux PUBLIC "${LINUX}" "${CORE}/login")
target_link_libraries(orbita_linux PUBLIC PkgConfig::CURL Threads::Threads)
target_compile_options(orbita_linux PRIVATE -Wall -Wextra -Wpedantic)

add_library(orbita_stream STATIC src/stream.c "${LINUX}/xc_webrtc_linux.c"
    "${CORE}/session/xc_session.c" "${CORE}/session/xs_machine.c"
    "${CORE}/webrtc/xc_webrtc.c" "${CORE}/webrtc/xc_webrtc_xsm.c"
    "${CORE}/webrtc/xc_proto.c" "${CORE}/webrtc/xc_jitter.c")
target_include_directories(orbita_stream PUBLIC "${CORE}/webrtc" "${CORE}/session" "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PRIVATE "${ORBITA_WEBRTC_ROOT}/include")
target_link_libraries(orbita_stream PUBLIC orbita_linux PkgConfig::AV PkgConfig::SDL2 PkgConfig::OPUS
    "${ORBITA_WEBRTC_ROOT}/lib/libpeer-xc.a" "${ORBITA_WEBRTC_ROOT}/lib/libsrtp2-xc.a"
    "${ORBITA_WEBRTC_ROOT}/lib/libusrsctp-xc.a" "${ORBITA_WEBRTC_ROOT}/lib/libmbedtls-xcwebrtc.a" m)
target_compile_options(orbita_stream PRIVATE -Wall -Wextra)

set(ORBITA_MENU_SOURCES src/catalog_view.c
    "${CORE}/login/login_flow.c" "${CORE}/login/xbox_auth.c"
    "${CORE}/login/cjson/cJSON.c" "${CORE}/ui/xui_gfx.c" "${CORE}/ui/xui_text.c")
add_executable(orbita src/main.c ${ORBITA_MENU_SOURCES})
target_include_directories(orbita PRIVATE "${CORE}/ui")
target_compile_definitions(orbita PRIVATE XCLOUD360_BETA=0 ORBITA_VERSION="${PROJECT_VERSION}")
target_compile_options(orbita PRIVATE -Wall -Wextra)
target_link_libraries(orbita PRIVATE orbita_stream)
install(TARGETS orbita RUNTIME DESTINATION orbita/bin)
install(PROGRAMS packaging/Orbita.sh DESTINATION .)

if(ORBITA_BUILD_VIDEO_TEST)
    add_executable(orbita-video-test src/video_test.c)
    target_compile_options(orbita-video-test PRIVATE -Wall -Wextra)
    target_link_libraries(orbita-video-test PRIVATE PkgConfig::AV PkgConfig::SDL2 m)
    install(TARGETS orbita-video-test RUNTIME DESTINATION orbita/bin)
endif()

if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_executable(menu_test tests/menu_test.c ${ORBITA_MENU_SOURCES})
    target_include_directories(menu_test PRIVATE "${CORE}/ui")
    target_compile_definitions(menu_test PRIVATE XCLOUD360_BETA=0 ORBITA_VERSION="${PROJECT_VERSION}")
    target_compile_options(menu_test PRIVATE -Wall -Wextra)
    target_link_libraries(menu_test PRIVATE orbita_stream)
    add_test(NAME catalog_search_and_controller COMMAND menu_test)
    set_tests_properties(catalog_search_and_controller PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    add_executable(linux_test tests/linux_test.c)
    target_link_libraries(linux_test PRIVATE orbita_linux)
    add_test(NAME linux_storage_and_clock COMMAND linux_test)
    add_executable(https_test tests/https_test.c "${LINUX}/https_curl.c")
    target_include_directories(https_test PRIVATE "${CORE}/login")
    target_compile_definitions(https_test PRIVATE ORBITA_TRANSPORT_TEST=1)
    target_link_libraries(https_test PRIVATE orbita_linux)
    add_test(NAME linux_https COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_https.py" "$<TARGET_FILE:https_test>")
    add_test(NAME sdl_screen_and_controller COMMAND orbita --smoke-test)
    set_tests_properties(sdl_screen_and_controller PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    add_test(NAME portmaster_launcher COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_launcher.py")
    add_executable(session_protocol_test "${REPO}/app/h264play/hosttest/session/session_test.c"
        "${REPO}/app/h264play/hosttest/session/mock_port.c"
        "${CORE}/session/xc_session.c" "${CORE}/session/xs_machine.c"
        "${CORE}/login/xbox_auth.c" "${CORE}/login/cjson/cJSON.c")
    target_include_directories(session_protocol_test PRIVATE "${CORE}/login")
    target_link_libraries(session_protocol_test PRIVATE Threads::Threads m)
    add_test(NAME xcloud_session_protocol COMMAND session_protocol_test)
    add_executable(catalog_test tests/catalog_test.c
        "${REPO}/app/h264play/hosttest/session/mock_port.c"
        "${CORE}/login/login_flow.c" "${CORE}/login/xbox_auth.c" "${CORE}/login/cjson/cJSON.c")
    target_include_directories(catalog_test PRIVATE "${CORE}/login" "${REPO}/app/h264play/hosttest/session")
    target_compile_options(catalog_test PRIVATE -Wall -Wextra)
    target_link_libraries(catalog_test PRIVATE Threads::Threads m)
    add_test(NAME xcloud_full_catalog COMMAND catalog_test)
    set(ORBITA_PION_PEER "" CACHE FILEPATH "Optional local Pion test peer binary")
    if(ORBITA_PION_PEER)
        add_test(NAME local_webrtc_stream COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_stream.py"
            "$<TARGET_FILE:orbita>" "${ORBITA_PION_PEER}")
        set_tests_properties(local_webrtc_stream PROPERTIES TIMEOUT 40)
    endif()
endif()
