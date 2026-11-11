cmake_minimum_required(VERSION 3.26)
project(RecubinEmbeddingExample LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Recubin CONFIG REQUIRED)
get_target_property(RECUBIN_INCLUDE_DIR Recubin::Runtime INTERFACE_INCLUDE_DIRECTORIES)
list(GET RECUBIN_INCLUDE_DIR 0 RECUBIN_INCLUDE_ROOT)
get_filename_component(RECUBIN_SDK_ROOT "${RECUBIN_INCLUDE_ROOT}/.." ABSOLUTE)
add_executable(RecubinEmbeddingExample main.cpp)
target_link_libraries(RecubinEmbeddingExample PRIVATE Recubin::Runtime)

add_custom_command(TARGET RecubinEmbeddingExample POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/sample_scene.yaml"
        "$<TARGET_FILE_DIR:RecubinEmbeddingExample>/sample_scene.yaml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/sample_scene_alt.yaml"
        "$<TARGET_FILE_DIR:RecubinEmbeddingExample>/sample_scene_alt.yaml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/sample_script.luau"
        "$<TARGET_FILE_DIR:RecubinEmbeddingExample>/sample_script.luau"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${RECUBIN_SDK_ROOT}/assets/fonts"
        "$<TARGET_FILE_DIR:RecubinEmbeddingExample>/assets/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${RECUBIN_SDK_ROOT}/shaders"
        "$<TARGET_FILE_DIR:RecubinEmbeddingExample>/shaders"
)
