cmake_minimum_required(VERSION 3.22)
project(LabHammer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(LAB_ENGINE_DIR "$ENV{LAB_ENGINE_DIR}")
if(NOT LAB_ENGINE_DIR)
    get_filename_component(LAB_ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
    if(NOT EXISTS "${LAB_ENGINE_DIR}/include")
        set(LAB_ENGINE_DIR "C:/Users/jancz/Desktop/Lab")
    endif()
endif()

include_directories(
    "${LAB_ENGINE_DIR}/include"
    "${LAB_ENGINE_DIR}/src/glad/include"
    "${LAB_ENGINE_DIR}/external/lua"
    "${LAB_ENGINE_DIR}/build/_deps/glfw-src/include"
)

link_directories(
    "${LAB_ENGINE_DIR}/build/Release"
    "${LAB_ENGINE_DIR}/build/_deps/glfw-build/src/Release"
    "${LAB_ENGINE_DIR}/bin"
)

add_executable(LabHammer src/editor_main.cpp)
target_compile_definitions(LabHammer PRIVATE GLAD_API_CALL_EXPORT)

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
    glfw3
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
)

set(ENGINE_DLLS
    glad.dll
    LabCore.dll
    LabRender.dll
    LabAudio.dll
    LabPhysics.dll
    LabAnimation.dll
    LabWorld.dll
    LabNetwork.dll
    LabAI.dll
    LabScript.dll
    LabStudioCore.dll
)

foreach(dll ${ENGINE_DLLS})
    add_custom_command(TARGET LabHammer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LAB_ENGINE_DIR}/build/Release/${dll}"
            "$<TARGET_FILE_DIR:LabHammer>/${dll}"
    )
endforeach()


