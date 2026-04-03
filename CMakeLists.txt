cmake_minimum_required(VERSION 3.10)
project(Recubin)

# C++17を使う（今後クォータニオンなどで便利）
set(CMAKE_CXX_STANDARD 17)

# src フォルダ以下のすべての .cpp ファイルを自動でリストアップ
file(GLOB_RECURSE SOURCES "src/*.cpp")

# インクルードパスの指定（プロジェクトルートを基点に）
include_directories(.)

# プリプロセッサ定義（バッチの /D と同じ）
add_definitions(-DGLEW_STATIC)

# 実行ファイルの設定
add_executable(Recubin ${SOURCES})

# ライブラリの検索パス
target_link_directories(Recubin PRIVATE ${CMAKE_SOURCE_DIR}/include/libs)

# リンクするライブラリ（バッチの .lib と同じ）
target_link_libraries(Recubin 
    glfw3 
    glew32s 
    opengl32 
    user32 
    gdi32 
    shell32
)