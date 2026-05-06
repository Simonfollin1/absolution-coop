cmake_minimum_required(VERSION 3.8)
project(SpeedrunToolkit CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE SOURCES "src/*.cpp")

add_library(SpeedrunToolkit SHARED ${SOURCES})

target_include_directories(SpeedrunToolkit PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(SpeedrunToolkit PRIVATE
    HitmanAbsolutionSDK
)

set_target_properties(SpeedrunToolkit PROPERTIES
    OUTPUT_NAME "SpeedrunToolkit"
)

if(HITMAN_ABSOLUTION_INSTALL_DIR AND IS_DIRECTORY "${HITMAN_ABSOLUTION_INSTALL_DIR}")
    install(TARGETS SpeedrunToolkit
            RUNTIME DESTINATION "${HITMAN_ABSOLUTION_INSTALL_DIR}/mods")
endif()
