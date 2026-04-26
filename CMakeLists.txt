cmake_minimum_required(VERSION 3.26)
project(Recubin)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- 1. MSVC ランタイム設定 (修正済み) ---
# /MD (Release) と /MDd (Debug) を適切に切り替えます
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# --- 2. グローバル設定 ---
add_compile_options(
    "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>"
    "$<$<CXX_COMPILER_ID:MSVC>:/Zi>"
    "$<$<CXX_COMPILER_ID:MSVC>:/MP>"
    "$<$<CXX_COMPILER_ID:MSVC>:/permissive->"
)

# --- 3. ソース収集 ---
file(GLOB_RECURSE SOURCES "src/*.cpp")

add_executable(Recubin ${SOURCES})

# --- 4. 静的リンクの強制定義 (重要) ---
# ここで定義することで、yaml-cppのヘッダーが __declspec(dllimport) を付けるのを防ぎます
target_compile_definitions(Recubin PRIVATE 
    YAML_CPP_STATIC_LIBRARIES
    GLEW_STATIC
)

# --- 5. インクルードパス ---
target_include_directories(Recubin PRIVATE 
    ${CMAKE_SOURCE_DIR}/include 
    ${CMAKE_SOURCE_DIR}/include/imgui
    .
)

# --- 6. リンク設定 ---
set(LIB_DIR "${CMAKE_SOURCE_DIR}/include/libs")

target_link_options(Recubin PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/NODEFAULTLIB:LIBCMT>)

target_link_libraries(Recubin PRIVATE
    "${LIB_DIR}/glfw3.lib"
    "${LIB_DIR}/glew32s.lib"
    opengl32 user32 gdi32 shell32
    "${LIB_DIR}/PhysX_64.lib"
    "${LIB_DIR}/PhysXCommon_64.lib"
    "${LIB_DIR}/PhysXFoundation_64.lib"
    "${LIB_DIR}/PhysXExtensions_static_64.lib"
    "${LIB_DIR}/Luau.Ast.lib"
    "${LIB_DIR}/Luau.Compiler.lib"
    "${LIB_DIR}/Luau.VM.lib"
    "${LIB_DIR}/Luau.Common.lib"
    "${LIB_DIR}/yaml-cpp.lib"
)