cmake_minimum_required(VERSION 3.26)
project(Recubin)

if(APPLE)
    enable_language(OBJCXX)
endif()

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

# --- 2.6. Box3D ---
set(BOX3D_SAMPLES OFF CACHE BOOL "" FORCE)
set(BOX3D_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX3D_DOCS OFF CACHE BOOL "" FORCE)
set(BOX3D_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(BOX3D_PROFILE OFF CACHE BOOL "" FORCE)
set(BOX3D_VALIDATE OFF CACHE BOOL "" FORCE)
set(BOX3D_SANITIZE OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/temp_libs/box3d EXCLUDE_FROM_ALL)
if(MSVC)
    set_target_properties(box3d PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()

# --- 3. ソース収集 ---
# Recubin / RecubinEngine の二重コンパイルを避けるため、両ターゲットで共有できる
# ソース（エントリポイントと EDITOR_DISABLED で分岐する User.cpp/Renderer.cpp を除く全て）を
# RecubinCore という OBJECT ライブラリとして1回だけコンパイルし、各ターゲットはそれをリンクする。
file(GLOB_RECURSE ALL_SOURCES_CPP "src/*.cpp")
file(GLOB_RECURSE ALL_SOURCES_C "src/*.c")
set(ALL_SOURCES ${ALL_SOURCES_CPP} ${ALL_SOURCES_C})
if(APPLE)
    list(APPEND ALL_SOURCES "${CMAKE_SOURCE_DIR}/src/Util/MacPlatform.mm")
endif()
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
set_source_files_properties("${CMAKE_SOURCE_DIR}/src/Core/TimeStretchNode.cpp" PROPERTIES COMPILE_OPTIONS
    "$<$<CXX_COMPILER_ID:MSVC>:/O2>;$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O3>"
)

add_library(RecubinCore OBJECT ${CORE_SOURCES})
target_include_directories(RecubinCore PRIVATE
    "${CMAKE_SOURCE_DIR}/temp_libs/box3d/include"
)

if(WIN32)
    add_executable(Watcher
        "${CMAKE_SOURCE_DIR}/src/watcher.cpp"
    )
    target_compile_definitions(Watcher PRIVATE
        WINVER=0x0600
        _WIN32_WINNT=0x0600
    )
    target_link_libraries(Watcher PRIVATE comctl32)
endif()

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

# The macOS package regression signs a real RecubinEngine Mach-O into its
# temporary App Bundle, so ensure it exists before RecubinTest is run.
if(APPLE)
    add_dependencies(RecubinTest RecubinEngine)
endif()

# --- 4. 静的リンクの強制定義 (重要) ---
set(LIB_DIR "${CMAKE_SOURCE_DIR}/include/libs")
set(COMMON_INCLUDES ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/include/imgui
    ${CMAKE_SOURCE_DIR}/include/PhysX
    ${CMAKE_SOURCE_DIR}/temp_libs/recastnavigation/Recast/Include
    ${CMAKE_SOURCE_DIR}/temp_libs/recastnavigation/Detour/Include
    ${CMAKE_SOURCE_DIR}/temp_libs/signalsmith-stretch/include
    .)

if(WIN32)
    set(COMMON_DEFS
        YAML_CPP_STATIC_LIBRARIES 
        GLEW_STATIC 
        NOMINMAX 
        $<$<CONFIG:Debug>:NDEBUG>
    )

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
        "${LIB_DIR}/Luau.Bytecode.lib"
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
    # GLFW / GLEW は配布先のHomebrew dylibに依存しないよう、
    # Homebrew等でインストールされた静的archiveを明示的に解決する。
    #   brew install glfw glew
    set(COMMON_DEFS
        YAML_CPP_STATIC_LIBRARIES 
        GLEW_STATIC 
        NOMINMAX 
        $<$<CONFIG:Debug>:NDEBUG>
    )

    set(RECUBIN_ORIGINAL_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
    find_library(RECUBIN_GLFW_STATIC_LIBRARY NAMES glfw3 glfw REQUIRED)
    find_library(RECUBIN_GLEW_STATIC_LIBRARY NAMES GLEW glew REQUIRED)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ${RECUBIN_ORIGINAL_FIND_LIBRARY_SUFFIXES})
    if(NOT RECUBIN_GLFW_STATIC_LIBRARY MATCHES "\\.a$" OR
       NOT RECUBIN_GLEW_STATIC_LIBRARY MATCHES "\\.a$")
        message(FATAL_ERROR "Mac向けGLFW/GLEWには静的archive (.a) が必要です。")
    endif()

    find_path(RECUBIN_GLFW_INCLUDE_DIR NAMES GLFW/glfw3.h REQUIRED)
    find_path(RECUBIN_GLEW_INCLUDE_DIR NAMES GL/glew.h REQUIRED)
    list(APPEND COMMON_INCLUDES
        ${RECUBIN_GLFW_INCLUDE_DIR}
        ${RECUBIN_GLEW_INCLUDE_DIR}
    )
    find_package(OpenGL REQUIRED)

    include(FetchContent)

    # Luau: リポジトリ内にビルド元のバージョン記録が無いため、
    # 既定でmasterを取得する。RECUBIN_LUAU_GIT_TAGで固定バージョンに上書き可能。
    set(RECUBIN_LUAU_GIT_TAG "0.731" CACHE STRING "Luauの取得元git tag/コミット")
    FetchContent_Declare(
        Luau
        GIT_REPOSITORY https://github.com/luau-lang/luau.git
        GIT_TAG ${RECUBIN_LUAU_GIT_TAG}
    )
    FetchContent_MakeAvailable(Luau)

    # yaml-cpp: Homebrew版には同梱patched headerが使う未リリースAPIが無く、最新masterは
    # convert<T, Enable>へ変更され同梱ヘッダのconvert<T>とリンク互換性がないため、
    # 以前の成功実績があり同梱ヘッダと一致するコミットを既定にする。旧既定値masterだけは
    # 一度移行し、ユーザーが指定した別のタグ/コミットは維持する。
    set(RECUBIN_YAMLCPP_DEFAULT_GIT_TAG "356643ba267039367d87fa70ce3d8f87144f55e9")
    if(DEFINED RECUBIN_YAMLCPP_GIT_TAG AND RECUBIN_YAMLCPP_GIT_TAG STREQUAL "master")
        set(RECUBIN_YAMLCPP_GIT_TAG "${RECUBIN_YAMLCPP_DEFAULT_GIT_TAG}" CACHE STRING "同梱ヘッダと互換性があるyaml-cppのgitコミット" FORCE)
    else()
        set(RECUBIN_YAMLCPP_GIT_TAG "${RECUBIN_YAMLCPP_DEFAULT_GIT_TAG}" CACHE STRING "同梱ヘッダと互換性があるyaml-cppのgitコミット")
    endif()
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
        ${RECUBIN_GLFW_STATIC_LIBRARY}
        yaml-cpp
        ${RECUBIN_GLEW_STATIC_LIBRARY}
        OpenGL::GL
        "-framework Cocoa"
        "-framework UniformTypeIdentifiers"
        "-framework IOKit"
        "-framework CoreFoundation"
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
target_link_libraries(Recubin PRIVATE ${COMMON_LIBS} box3d::box3d)

# ---- RecubinEngine (ゲームランタイム専用) ----
target_compile_definitions(RecubinEngine PRIVATE ${COMMON_DEFS} EDITOR_DISABLED)
target_include_directories(RecubinEngine PRIVATE ${COMMON_INCLUDES})
target_link_options(RecubinEngine PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/NODEFAULTLIB:LIBCMT>
    $<$<CXX_COMPILER_ID:MSVC>:/DEBUG>
    $<$<CXX_COMPILER_ID:MSVC>:/ignore:4099>
)
target_link_libraries(RecubinEngine PRIVATE ${COMMON_LIBS} box3d::box3d)

# ---- RecubinTest (ヘッドレスLuauテストランナー) ----
target_compile_definitions(RecubinTest PRIVATE ${COMMON_DEFS} EDITOR_DISABLED)
target_include_directories(RecubinTest PRIVATE ${COMMON_INCLUDES})
target_link_options(RecubinTest PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/NODEFAULTLIB:LIBCMT>
    $<$<CXX_COMPILER_ID:MSVC>:/DEBUG>
    $<$<CXX_COMPILER_ID:MSVC>:/ignore:4099>
)
target_link_libraries(RecubinTest PRIVATE ${COMMON_LIBS} box3d::box3d)

if(WIN32)
    foreach(target IN ITEMS Recubin RecubinEngine RecubinTest)
        target_link_libraries(${target} PRIVATE delayimp)
        target_link_options(${target} PRIVATE
            $<$<CXX_COMPILER_ID:MSVC>:/DELAYLOAD:PhysX_64.dll>
            $<$<CXX_COMPILER_ID:MSVC>:/DELAYLOAD:PhysXCommon_64.dll>
            $<$<CXX_COMPILER_ID:MSVC>:/DELAYLOAD:PhysXFoundation_64.dll>
            $<$<CXX_COMPILER_ID:MSVC>:/DELAYLOAD:PhysXCooking_64.dll>
        )
    endforeach()
endif()

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
