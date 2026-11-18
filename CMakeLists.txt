cmake_minimum_required(VERSION 3.20)
project(kalm VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

include(GNUInstallDirs)

set(KALM_DISPLAY_VERSION "" CACHE STRING "Displayed app version (numeric release tag or commit SHA)")
if(KALM_DISPLAY_VERSION STREQUAL "")
    find_package(Git QUIET)
    if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" describe --tags --exact-match HEAD
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE _exact_tag
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE _tag_rc
        )
        if(_tag_rc EQUAL 0 AND _exact_tag MATCHES "^v[0-9]+\\.[0-9]+(\\.[0-9]+)?$")
            set(KALM_DISPLAY_VERSION "${_exact_tag}")
        else()
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" rev-parse --short=7 HEAD
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                OUTPUT_VARIABLE _short_sha
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            if(NOT _short_sha STREQUAL "")
                set(KALM_DISPLAY_VERSION "${_short_sha}")
            endif()
        endif()
    endif()
    if(KALM_DISPLAY_VERSION STREQUAL "")
        set(KALM_DISPLAY_VERSION "v${PROJECT_VERSION}")
    endif()
endif()
add_compile_definitions(KALM_DISPLAY_VERSION="${KALM_DISPLAY_VERSION}")

option(ENABLE_SANITIZERS "Enable AddressSanitizer, LeakSanitizer, and UndefinedBehaviorSanitizer" OFF)
if(MSVC)
    add_compile_options(/W4 /utf-8 /EHsc)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(ENABLE_SANITIZERS)
        add_compile_options(-fsanitize=address,leak,undefined -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=address,leak,undefined)
    endif()
endif()

if(WIN32)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Qml Quick QuickControls2)
    set(KALM_PLATFORM_TRANSPORT src/transport_win.cpp)
else()
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Qml Quick DBus)
    set(KALM_PLATFORM_TRANSPORT src/transport_linux.cpp)
endif()

add_library(kalm_core STATIC
    src/huawei.cpp
    src/huawei.hpp
    src/i18n.cpp
    src/i18n.hpp
    src/probe.cpp
    src/probe.hpp
    src/protocol.cpp
    src/protocol.hpp
    src/transport_common.cpp
    ${KALM_PLATFORM_TRANSPORT}
    src/transport.hpp
    src/registry.cpp
    src/registry.hpp
    src/session.cpp
    src/session.hpp
)
target_include_directories(kalm_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(kalm_core PUBLIC Qt6::Core)

if(WIN32)
    target_link_libraries(kalm_core PUBLIC ws2_32 bthprops)
else()
    target_link_libraries(kalm_core PUBLIC Qt6::DBus)
endif()

set(KALM_APP_SOURCES
    src/main.cpp
    src/backend.cpp
    src/backend.hpp
    resources.qrc
)
if(WIN32)
    list(APPEND KALM_APP_SOURCES src/kalm.rc)
endif()

add_executable(kalm WIN32 ${KALM_APP_SOURCES})
target_link_libraries(kalm PRIVATE
    kalm_core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Qml
    Qt6::Quick
)
if(WIN32)
    target_link_libraries(kalm PRIVATE Qt6::QuickControls2)
endif()

install(TARGETS kalm RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(NOT WIN32)
    install(CODE "
        set(_kalm_exec \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/kalm\")
        file(READ \"${CMAKE_CURRENT_SOURCE_DIR}/io.github.kalm.desktop.in\" _desktop_in)
        string(REPLACE \"@CMAKE_INSTALL_FULL_BINDIR@/kalm\" \"\${_kalm_exec}\" _desktop_out \"\${_desktop_in}\")
        set(_apps_dir \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATAROOTDIR}/applications\")
        file(MAKE_DIRECTORY \"\${_apps_dir}\")
        file(WRITE \"\${_apps_dir}/io.github.kalm.desktop\" \"\${_desktop_out}\")
        message(STATUS \"Installing: \${_apps_dir}/io.github.kalm.desktop (Exec=\${_kalm_exec})\")
    ")
endif()
install(DIRECTORY translations/ DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/kalm/translations)

option(BUILD_TESTING "Build unit tests" ON)
if(BUILD_TESTING)
    enable_testing()
    add_executable(test_kalm tests/test_kalm.cpp resources.qrc)
    target_link_libraries(test_kalm PRIVATE kalm_core)
    add_test(NAME test_kalm COMMAND test_kalm)
endif()
