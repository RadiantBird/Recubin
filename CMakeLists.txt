cmake_minimum_required(VERSION 3.26)
project(Recubin)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- 1. MSVC ランタイム設定 (修正済み) ---
# 依存ライブラリが Release ランタイム (/MD) でビルドされているため、
# Debug 構成でも /MD を使ってランタイム不一致を避けます
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

# --- 2. グローバル設定 ---
add_compile_options(
    "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>"
    "$<$<CXX_COMPILER_ID:MSVC>:/Zi>"
    "$<$<CXX_COMPILER_ID:MSVC>:/MP>"
    "$<$<CXX_COMPILER_ID:MSVC>:/permissive->"
)

# --- 3. ソース収集 ---
file(GLOB_RECURSE SOURCES "src/*.cpp")
list(FILTER SOURCES EXCLUDE REGEX ".*/game_main\\.cpp$")

add_executable(Recubin ${SOURCES})

# ---- RecubinEngine (ゲームランタイム専用) ----
file(GLOB_RECURSE GAME_SOURCES "src/*.cpp")
list(FILTER GAME_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")
list(APPEND GAME_SOURCES "${CMAKE_SOURCE_DIR}/src/game_main.cpp")
add_executable(RecubinEngine ${GAME_SOURCES})

# --- 4. 静的リンクの強制定義 (重要) ---
set(LIB_DIR "${CMAKE_SOURCE_DIR}/include/libs")
set(COMMON_DEFS YAML_CPP_STATIC_LIBRARIES GLEW_STATIC $<$<CONFIG:Debug>:NDEBUG>)
set(COMMON_INCLUDES ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/include/imgui .)
set(COMMON_LIBS
    "${LIB_DIR}/glfw3.lib"
    "${LIB_DIR}/glew32s.lib"
    opengl32 user32 gdi32 shell32
    "${LIB_DIR}/PhysX_64.lib"
    "${LIB_DIR}/PhysXCommon_64.lib"
    "${LIB_DIR}/PhysXFoundation_64.lib"
    "${LIB_DIR}/PhysXExtensions_static_64.lib"
    "${LIB_DIR}/PhysXCooking_64.lib"
    "${LIB_DIR}/Luau.Ast.lib"
    "${LIB_DIR}/Luau.Compiler.lib"
    "${LIB_DIR}/Luau.VM.lib"
    "${LIB_DIR}/Luau.Common.lib"
    "${LIB_DIR}/yaml-cpp.lib"
)

# ---- Recubin (エディター付き) ----
target_compile_definitions(Recubin PRIVATE ${COMMON_DEFS})
target_include_directories(Recubin PRIVATE ${COMMON_INCLUDES})
target_link_options(Recubin PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/NODEFAULTLIB:LIBCMT>
    $<$<CXX_COMPILER_ID:MSVC>:/DEBUG>
)
target_link_libraries(Recubin PRIVATE ${COMMON_LIBS})

# ---- RecubinEngine (ゲームランタイム専用) ----
target_compile_definitions(RecubinEngine PRIVATE ${COMMON_DEFS} EDITOR_DISABLED)
target_include_directories(RecubinEngine PRIVATE ${COMMON_INCLUDES})
target_link_options(RecubinEngine PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/NODEFAULTLIB:LIBCMT>
    $<$<CXX_COMPILER_ID:MSVC>:/DEBUG>
)
target_link_libraries(RecubinEngine PRIVATE ${COMMON_LIBS})
