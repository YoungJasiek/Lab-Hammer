cmake_minimum_required(VERSION 3.22)
project(LabHammer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(LAB_ENGINE_DIR "$ENV{LAB_ENGINE_DIR}")
if(NOT LAB_ENGINE_DIR)
    set(LAB_ENGINE_DIR "C:\Users\jancz\Desktop\Lab")
endif()

include_directories(
    "${LAB_ENGINE_DIR}/include"
    "${LAB_ENGINE_DIR}/src/glad/include"
    "${LAB_ENGINE_DIR}/external/lua"
)

link_directories(
    "${LAB_ENGINE_DIR}/build/Release"
    "${LAB_ENGINE_DIR}/bin"
)

add_executable(LabHammer src/editor_main.cpp)

target_link_libraries(LabHammer PRIVATE
    LabCore
    LabRender
    LabAudio
    LabPhysics
    LabAnimation
    LabWorld
    LabAI
    LabNetwork
    LabScript
    LabStudioCore
    glad
)

if(MSVC)
    target_compile_options(LabHammer PRIVATE /W4 /utf-8 /permissive-)
endif()

add_custom_command(TARGET LabHammer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:LabHammer>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/fonts"
        "$<TARGET_FILE_DIR:LabHammer>/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${LAB_ENGINE_DIR}/build/Release"
        "$<TARGET_FILE_DIR:LabHammer>"
)
