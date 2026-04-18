cmake_minimum_required(VERSION 3.26) # import std には新しいCMakeが必要
project(Recubin)

# C++23を有効化 (MSVC 19.50なら /std:c++latest が適用される)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- 1. グローバル設定 ---
add_compile_options(
    "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>"
    "$<$<CXX_COMPILER_ID:MSVC>:/Zi>"        # デバッグ情報
    "$<$<CXX_COMPILER_ID:MSVC>:/MP>"        # 並列コンパイル
    "$<$<CXX_COMPILER_ID:MSVC>:/permissive->" # 準拠モード (モダンC++には必須)
    "$<$<CXX_COMPILER_ID:MSVC>:/FA>"        # アセンブリコード生成
)

# --- 2. ソース収集 ---
# GLOBは便利ですが、ファイル追加時に再実行が必要なので注意
file(GLOB_RECURSE SOURCES "src/*.cpp")

# --- 3. ターゲット設定 ---
add_executable(Recubin ${SOURCES})

# GLEW_STATIC などの定義はターゲットに対して行うのがモダン
target_compile_definitions(Recubin PRIVATE GLEW_STATIC)

# --- 4. インクルードパスとライブラリパス ---
target_include_directories(Recubin PRIVATE 
    ${CMAKE_SOURCE_DIR}/include
    .
)

target_link_directories(Recubin PRIVATE 
    ${CMAKE_SOURCE_DIR}/include/libs
)

# --- 5. リンク設定 ---
# /NODEFAULTLIB:LIBCMT は PhysX や GLFW のビルド構成（MD/MT）に合わせて慎重に
target_link_options(Recubin PRIVATE "/NODEFAULTLIB:LIBCMT")

target_link_libraries(Recubin 
    # Graphics
    glfw3 
    glew32s 
    opengl32 
    user32 
    gdi32 
    shell32

    # PhysX 5 (名前が 64 付加のままでもパスが通っていればOK)
    PhysX_64
    PhysXCommon_64
    PhysXFoundation_64
    PhysXExtensions_static_64

    # Luau
    Luau.Ast
    Luau.Compiler
    Luau.VM

    # YAML
    yaml-cpp
)