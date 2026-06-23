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
    "$<$<CXX_COMPILER_ID:MSVC>:/Z7>"
    "$<$<CXX_COMPILER_ID:MSVC>:/MP>"
    "$<$<CXX_COMPILER_ID:MSVC>:/permissive->"
)

# --- 3. ソース収集 ---
# Recubin / RecubinEngine の二重コンパイルを避けるため、両ターゲットで共有できる
# ソース（エントリポイントと EDITOR_DISABLED で分岐する User.cpp/Renderer.cpp を除く全て）を
# RecubinCore という OBJECT ライブラリとして1回だけコンパイルし、各ターゲットはそれをリンクする。
file(GLOB_RECURSE ALL_SOURCES "src/*.cpp")
set(PER_TARGET_SOURCES
    "${CMAKE_SOURCE_DIR}/src/main.cpp"
    "${CMAKE_SOURCE_DIR}/src/game_main.cpp"
    "${CMAKE_SOURCE_DIR}/src/test_main.cpp"
    "${CMAKE_SOURCE_DIR}/src/Core/User.cpp"
    "${CMAKE_SOURCE_DIR}/src/Core/Renderer.cpp"
)
set(CORE_SOURCES ${ALL_SOURCES})
list(REMOVE_ITEM CORE_SOURCES ${PER_TARGET_SOURCES})

add_library(RecubinCore OBJECT ${CORE_SOURCES})

add_executable(Recubin
    "${CMAKE_SOURCE_DIR}/src/main.cpp"
    "${CMAKE_SOURCE_DIR}/src/Core/User.cpp"
    "${CMAKE_SOURCE_DIR}/src/Core/Renderer.cpp"
    $<TARGET_OBJECTS:RecubinCore>
)

# ---- RecubinEngine (ゲームランタイム専用) ----
add_executable(RecubinEngine
    "${CMAKE_SOURCE_DIR}/src/game_main.cpp"
    "${CMAKE_SOURCE_DIR}/src/Core/User.cpp"
    "${CMAKE_SOURCE_DIR}/src/Core/Renderer.cpp"
    $<TARGET_OBJECTS:RecubinCore>
)

# ---- RecubinTest (ヘッドレスLuauテストランナー) ----
add_executable(RecubinTest
    "${CMAKE_SOURCE_DIR}/src/test_main.cpp"
    "${CMAKE_SOURCE_DIR}/src/Core/User.cpp"
    "${CMAKE_SOURCE_DIR}/src/Core/Renderer.cpp"
    $<TARGET_OBJECTS:RecubinCore>
)

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

# ---- RecubinCore (共有オブジェクトライブラリ) ----
target_compile_definitions(RecubinCore PRIVATE ${COMMON_DEFS})
target_include_directories(RecubinCore PRIVATE ${COMMON_INCLUDES})

# ---- Recubin (エディター付き) ----
target_compile_definitions(Recubin PRIVATE ${COMMON_DEFS})
target_include_directories(Recubin PRIVATE ${COMMON_INCLUDES})
target_link_options(Recubin PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/NODEFAULTLIB:LIBCMT>
    $<$<CXX_COMPILER_ID:MSVC>:/DEBUG>
    $<$<CXX_COMPILER_ID:MSVC>:/ignore:4099>
)
target_link_libraries(Recubin PRIVATE ${COMMON_LIBS})

# ---- RecubinEngine (ゲームランタイム専用) ----
target_compile_definitions(RecubinEngine PRIVATE ${COMMON_DEFS} EDITOR_DISABLED)
target_include_directories(RecubinEngine PRIVATE ${COMMON_INCLUDES})
target_link_options(RecubinEngine PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/NODEFAULTLIB:LIBCMT>
    $<$<CXX_COMPILER_ID:MSVC>:/DEBUG>
    $<$<CXX_COMPILER_ID:MSVC>:/ignore:4099>
)
target_link_libraries(RecubinEngine PRIVATE ${COMMON_LIBS})

# ---- RecubinTest (ヘッドレスLuauテストランナー) ----
target_compile_definitions(RecubinTest PRIVATE ${COMMON_DEFS} EDITOR_DISABLED)
target_include_directories(RecubinTest PRIVATE ${COMMON_INCLUDES})
target_link_options(RecubinTest PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/NODEFAULTLIB:LIBCMT>
    $<$<CXX_COMPILER_ID:MSVC>:/DEBUG>
    $<$<CXX_COMPILER_ID:MSVC>:/ignore:4099>
)
target_link_libraries(RecubinTest PRIVATE ${COMMON_LIBS})

# ==============================================================================
# --- 5. DLLの自動コピー設定 (Visual Studio対応) ---
# ==============================================================================
# if(WIN32)
#     # コピー元となるDLLフォルダのパス
#     set(DLL_SOURCE_DIR "${CMAKE_SOURCE_DIR}/dlls")

#     # Recubin (エディター付き) の出力先へコピー
#     add_custom_command(TARGET Recubin POST_BUILD
#         COMMAND ${CMAKE_COMMAND} -E copy_directory
#         "${DLL_SOURCE_DIR}"
#         "$<TARGET_FILE_DIR:Recubin>"
#         COMMENT "[CMake] Copying DLLs to Recubin output directory..."
#     )

#     # RecubinEngine (ゲームランタイム) の出力先へコピー
#     add_custom_command(TARGET RecubinEngine POST_BUILD
#         COMMAND ${CMAKE_COMMAND} -E copy_directory
#         "${DLL_SOURCE_DIR}"
#         "$<TARGET_FILE_DIR:RecubinEngine>"
#         COMMENT "[CMake] Copying DLLs to RecubinEngine output directory..."
#     )
# endif()