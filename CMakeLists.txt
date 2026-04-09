cmake_minimum_required(VERSION 3.24)

project(drpc VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(DRPC_SDK_ROOT "${CMAKE_SOURCE_DIR}/vendor/discord_social_sdk" CACHE PATH "Path to the Discord Social SDK package root")
set(DRPC_SDK_INCLUDE_DIR "${DRPC_SDK_ROOT}/include" CACHE PATH "Path to the Discord Social SDK include directory")
set(DRPC_SDK_LIBRARY_HINTS
    "${DRPC_SDK_ROOT}/lib"
    "${DRPC_SDK_ROOT}/lib/debug"
    "${DRPC_SDK_ROOT}/lib/release"
    "${DRPC_SDK_ROOT}/lib/windows"
    "${DRPC_SDK_ROOT}/lib/windows/x64"
    "${DRPC_SDK_ROOT}/lib/x64")
set(DRPC_SDK_DLL_HINTS
    "${DRPC_SDK_ROOT}/bin"
    "${DRPC_SDK_ROOT}/bin/debug"
    "${DRPC_SDK_ROOT}/bin/release"
    "${DRPC_SDK_ROOT}/bin/windows"
    "${DRPC_SDK_ROOT}/bin/windows/x64")

find_library(DRPC_SDK_LIBRARY
    NAMES discord_partner_sdk discord_partner_sdk_static
    PATHS ${DRPC_SDK_LIBRARY_HINTS}
    NO_DEFAULT_PATH)

find_file(DRPC_SDK_DLL
    NAMES discord_partner_sdk.dll
    PATHS ${DRPC_SDK_DLL_HINTS}
    NO_DEFAULT_PATH)

add_executable(drpc WIN32
    src/main.cpp
    src/ActivityPreset.h
    src/Config.cpp
    src/Config.h
    src/DiscordPresenceService.cpp
    src/DiscordPresenceService.h
    src/Logging.cpp
    src/Logging.h
    src/MockPresenceSource.cpp
    src/MockPresenceSource.h
    src/PresenceSource.h
    src/TrayApplication.cpp
    src/TrayApplication.h)

target_compile_definitions(drpc PRIVATE NOMINMAX UNICODE _UNICODE WIN32_LEAN_AND_MEAN)

target_include_directories(drpc PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/vendor")

target_link_libraries(drpc PRIVATE shell32)

if(EXISTS "${DRPC_SDK_INCLUDE_DIR}/discordpp.h" AND DRPC_SDK_LIBRARY)
    message(STATUS "Discord Social SDK enabled")
    target_compile_definitions(drpc PRIVATE DRPC_WITH_DISCORD_SDK=1)
    target_include_directories(drpc PRIVATE "${DRPC_SDK_INCLUDE_DIR}")
    target_link_libraries(drpc PRIVATE "${DRPC_SDK_LIBRARY}")

    if(DRPC_SDK_DLL)
        add_custom_command(TARGET drpc POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${DRPC_SDK_DLL}"
                "$<TARGET_FILE_DIR:drpc>/discord_partner_sdk.dll")
    endif()
else()
    message(WARNING "Discord Social SDK not found. The tray app will build with a no-op backend until you add the SDK under vendor/discord_social_sdk or configure DRPC_SDK_INCLUDE_DIR and DRPC_SDK_LIBRARY.")
endif()

add_custom_command(TARGET drpc POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config.json"
        "$<TARGET_FILE_DIR:drpc>/config.json")
