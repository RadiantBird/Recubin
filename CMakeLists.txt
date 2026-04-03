cmake_minimum_required(VERSION 3.10)
project(Recubin)

set(CMAKE_CXX_STANDARD 17)

# --- 1. 準備：ファイル収集とインクルードパス ---
file(GLOB_RECURSE SOURCES "src/*.cpp")
include_directories(.)
add_definitions(-DGLEW_STATIC)

# コンパイルオプションはターゲットに関係なく全体にかけられるのでここでOK
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

# --- 2. ターゲット作成：ここで "Recubin" が誕生する ---
add_executable(Recubin ${SOURCES})

# --- 3. ターゲットに対する詳細設定：誕生した後なら設定可能 ---
set_target_properties(Recubin PROPERTIES LINK_FLAGS "/NODEFAULTLIB:LIBCMT")

target_link_directories(Recubin PRIVATE ${CMAKE_SOURCE_DIR}/include/libs)
include_directories(${CMAKE_SOURCE_DIR}/include/PhysX)
link_directories(${CMAKE_SOURCE_DIR}/include/libs)

target_link_libraries(Recubin 
    glfw3 
    glew32s 
    opengl32 
    user32 
    gdi32 
    shell32

    PhysX_64
    PhysXCommon_64
    PhysXFoundation_64
    PhysXExtensions_static_64
)