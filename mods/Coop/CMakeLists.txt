cmake_minimum_required(VERSION 3.8)
project(Coop CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Sources from this mod plus the shared code, which the packaging step copies
# into this folder as "common".
file(GLOB_RECURSE SOURCES
    "src/*.cpp"
    "common/src/*.cpp"
)

add_library(Coop SHARED ${SOURCES})

target_include_directories(Coop PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/common/include"
)

# ws2_32 is the whole networking dependency. No ENet, no GameNetworkingSockets:
# the traffic is a few hundred bytes a second, the build is 32-bit, and the
# vcpkg manifest belongs to the SDK repository - a dependency here would cost
# more in build fragility than it saves in code.
target_link_libraries(Coop PRIVATE
    HitmanAbsolutionSDK
    ws2_32
)

set_target_properties(Coop PROPERTIES
    OUTPUT_NAME "Coop"
)

if(HITMAN_ABSOLUTION_INSTALL_DIR AND IS_DIRECTORY "${HITMAN_ABSOLUTION_INSTALL_DIR}")
    install(TARGETS Coop
            RUNTIME DESTINATION "${HITMAN_ABSOLUTION_INSTALL_DIR}/mods")
endif()
