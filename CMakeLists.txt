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

# --- 2.5. Recast/Detour (パスファインディング用ナビメッシュライブラリ) ---
set(RECASTNAVIGATION_DEMO OFF CACHE BOOL "" FORCE)
set(RECASTNAVIGATION_TESTS OFF CACHE BOOL "" FORCE)
set(RECASTNAVIGATION_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/temp_libs/recastnavigation)

# --- 3. ソース収集 ---
# Recubin / RecubinEngine の二重コンパイルを避けるため、両ターゲットで共有できる
# ソース（エントリポイントと EDITOR_DISABLED で分岐する User.cpp/Renderer.cpp を除く全て）を
# RecubinCore という OBJECT ライブラリとして1回だけコンパイルし、各ターゲットはそれをリンクする。
file(GLOB_RECURSE ALL_SOURCES_CPP "src/*.cpp")
file(GLOB_RECURSE ALL_SOURCES_C "src/*.c")
set(ALL_SOURCES ${ALL_SOURCES_CPP} ${ALL_SOURCES_C})
set(PER_TARGET_SOURCES
    "${CMAKE_SOURCE_DIR}/src/main.cpp"
    "${CMAKE_SOURCE_DIR}/src/game_main.cpp"
    "${CMAKE_SOURCE_DIR}/src/test_main.cpp"
    "${CMAKE_SOURCE_DIR}/src/watcher.cpp"
    "${CMAKE_SOURCE_DIR}/src/Core/User.cpp"
    "${CMAKE_SOURCE_DIR}/src/Core/Renderer.cpp"
)
set(CORE_SOURCES ${ALL_SOURCES})
list(REMOVE_ITEM CORE_SOURCES ${PER_TARGET_SOURCES})

# --- シャドーイング検出（デフォルト無効の警告を明示的に有効化） ---
# /w14456: ローカル変数が外側スコープのローカル変数を隠す
# /w14457: ローカル変数が関数パラメータを隠す
# /w14458: ローカル変数がメンバ変数を隠す
# LuauEngine.cpp は Lua/Luau C API の慣習で `lua_State* L` という引数名を
# 意図的にメンバー変数 L (メインVM state) と同名にしている（コルーチン等、メンバーとは
# 別の state を渡すための設計）ため、C4458 が大量に誤検出される。有効化の対象から除外する
# （AdditionalOptions で後から /wd4458 を足しても DisableSpecificWarnings の評価順の都合で
#  上書きできないため、最初から適用しない方式にする）。
set(SHADOW_CHECK_SOURCES ${ALL_SOURCES})
list(REMOVE_ITEM SHADOW_CHECK_SOURCES "${CMAKE_SOURCE_DIR}/src/Core/LuauEngine.cpp")
set_source_files_properties(${SHADOW_CHECK_SOURCES} PROPERTIES COMPILE_OPTIONS
    "$<$<CXX_COMPILER_ID:MSVC>:/w14456>;$<$<CXX_COMPILER_ID:MSVC>:/w14457>;$<$<CXX_COMPILER_ID:MSVC>:/w14458>"
)

add_library(RecubinCore OBJECT ${CORE_SOURCES})

add_executable(Watcher
    "${CMAKE_SOURCE_DIR}/src/watcher.cpp"
)

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
set(COMMON_INCLUDES ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/include/imgui
    ${CMAKE_SOURCE_DIR}/include/PhysX
    ${CMAKE_SOURCE_DIR}/temp_libs/recastnavigation/Recast/Include
    ${CMAKE_SOURCE_DIR}/temp_libs/recastnavigation/Detour/Include
    .)

if(WIN32)
    set(COMMON_DEFS YAML_CPP_STATIC_LIBRARIES GLEW_STATIC $<$<CONFIG:Debug>:NDEBUG>)
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
        Recast
        Detour
        ws2_32
        winmm
    )
else()
    # --- Mac対応 ---
    # GLFW / GLEW は Windows用.libのプリビルドが無いため、
    # Homebrew等でインストールされたものを find_package で解決する想定。
    #   brew install glfw glew
    set(COMMON_DEFS YAML_CPP_STATIC_LIBRARIES $<$<CONFIG:Debug>:NDEBUG>)

    find_package(glfw3 REQUIRED)
    find_package(GLEW REQUIRED)
    find_package(OpenGL REQUIRED)

    include(FetchContent)

    # Luau: リポジトリ内にビルド元のバージョン記録が無いため、
    # 既定でmasterを取得する。RECUBIN_LUAU_GIT_TAGで固定バージョンに上書き可能。
    set(RECUBIN_LUAU_GIT_TAG "0.695" CACHE STRING "Luauの取得元git tag/コミット")
    FetchContent_Declare(
        Luau
        GIT_REPOSITORY https://github.com/luau-lang/luau.git
        GIT_TAG ${RECUBIN_LUAU_GIT_TAG}
    )
    FetchContent_MakeAvailable(Luau)

    # yaml-cpp: Homebrew版(0.9.0リリースタグ)には`include/yaml-cpp`に同梱されている
    # patched header(GetShowTrailingZero/insert_map_pairのforce引数等、jbeder/yaml-cpp
    # masterブランチには存在するがリリースタグ0.9.0には未リリースのAPI)が無く、
    # リンク時にシンボル不足で失敗する。ヘッダと一致するmasterブランチをソースから取得する。
    set(RECUBIN_YAMLCPP_GIT_TAG "master" CACHE STRING "yaml-cppの取得元git tag/コミット")
    set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG ${RECUBIN_YAMLCPP_GIT_TAG}
    )
    FetchContent_MakeAvailable(yaml-cpp)

    # PhysX: NVIDIA公式リポジトリにはmacOS向けビルドプリセットが存在しないため、
    # TARGET_BUILD_PLATFORM=linuxとして騙し、Apple Clang対応・SIMDフォールバック等を
    # 局所的にパッチしたPhysXソース(タグ107.3-physx-5.6.1)を自前ビルドしたものを指す。
    # 出力ライブラリ名にWindows版のような"_64"サフィックスは付かない。
    set(RECUBIN_PHYSX_MAC_DIR "" CACHE PATH "MacでビルドしたPhysX 5.6.1静的ライブラリ(.a)が置かれたディレクトリ")
    if(RECUBIN_PHYSX_MAC_DIR STREQUAL "")
        message(FATAL_ERROR
            "Mac向けビルドには RECUBIN_PHYSX_MAC_DIR の指定が必要です。"
            "TARGET_BUILD_PLATFORM=linuxとしてMac上で自前ビルドしたPhysX 5.6.1の.a群のディレクトリを"
            "-DRECUBIN_PHYSX_MAC_DIR=... で指定してください。")
    endif()

    set(PHYSX_MAC_COMPONENTS
        PhysX_static
        PhysXCommon_static
        PhysXFoundation_static
        PhysXExtensions_static
        PhysXCooking_static
    )
    set(PHYSX_MAC_LIBS "")
    foreach(component ${PHYSX_MAC_COMPONENTS})
        find_library(PHYSX_MAC_LIB_${component} NAMES ${component} PATHS ${RECUBIN_PHYSX_MAC_DIR} REQUIRED)
        list(APPEND PHYSX_MAC_LIBS ${PHYSX_MAC_LIB_${component}})
    endforeach()

    set(COMMON_LIBS
        glfw
        yaml-cpp
        GLEW::GLEW
        OpenGL::GL
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreVideo"
        ${PHYSX_MAC_LIBS}
        Luau.Ast
        Luau.Compiler
        Luau.VM
        Luau.Common
        Recast
        Detour
    )
endif()

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