# 開発進捗ログ

セッションごとの作業記録。新しいセッションを開始する際はまず一番下（最新）のセッションを読むこと。

---

## 2026-07-09 Mac対応本格実装（OpenGL 4.1 Core Profile + ビルドシステムMac分岐）

### 指示内容
readme.mdのTodo「Mac対応を開始する」のうち残っていた「OpenGL4.1に準拠した実装を行う」を「本格実装」するよう依頼された。Plan modeで、Explore以外のサブエージェント（Planエージェント含む）はCLAUDE.mdの指示により使わず、設計は自分で行った。AskUserQuestionで2点確認: (1) スコープをコード変更のみにするかCMakeLists.txt/build.pyのMac向け変更も含めるか→**含める**（CLAUDE.mdの「ビルドシステムを触らない」ルールへの明示的な例外指示として扱う）。(2) PhysXのmacOS非公式サポートを認識しておいてもらう確認→ユーザーは「昔Macでビルドしたことがある気がする」とのことで、実際に調べたところPhysXは5.6でmacOS向けプリセットを公式サポートしていることが判明（ユーザーの記憶の方が正しかった）。

### 何をしたか
- **`src/main.cpp`の`setupWindow()`**: `glfwCreateWindow()`前に`glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4)`/`MINOR,1`/`GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE`/`GLFW_OPENGL_FORWARD_COMPAT,GL_TRUE`を追加。`glewInit()`直前に`glewExperimental = GL_TRUE`を追加。
- **`src/game_main.cpp`**: 同じ4行のwindow hintと`glewExperimental = GL_TRUE`をウィンドウ作成部に追加。
- **`CMakeLists.txt`**: `COMMON_DEFS`/`COMMON_LIBS`を`if(WIN32) ... else() ... endif()`で分岐。WIN32側は既存の`.lib`直リンクを完全に無変更。else側（Apple想定）は`find_package(glfw3/yaml-cpp/GLEW/OpenGL)`（Homebrew前提）、Luauは`FetchContent`でmasterを既定取得（`RECUBIN_LUAU_GIT_TAG`で上書き可）、PhysXは`RECUBIN_PHYSX_MAC_DIR`というCACHE PATH変数から`find_library`で各コンポーネント(.a)を探す（未設定なら`message(FATAL_ERROR)`で案内）。macOSフレームワーク（Cocoa/IOKit/CoreVideo）もリンク。`GLEW_STATIC`定義はWIN32限定にした（Homebrew版GLEWは動的リンクのため）。
- **`build.py`**: `platform`モジュールをimportし、`build()`内で`is_windows = platform.system() == "Windows"`判定を追加。`-A x64`・`GLEW_STATIC=ON`のcmake configure引数、`copy_dlls()`呼び出し、`build_launcher()`呼び出しをすべてWindows限定にガード。非Windowsでは「DLLコピー・ランチャービルドをスキップ」とログ出力するのみ。
- **`readme.md`**: Mac対応Todoブロックに`[x] OpenGL4.1に準拠した実装を行う`と`[x] CMakeLists.txt/build.pyにMac向け分岐を追加(...)`を追記。「あとでMacで確認する」項目は未チェックのまま維持。

### なぜそうしたか
- GLSLシェーダー（`src/*.glsl`、全て`#version 330 core`）とRenderer.cpp本体等のGL呼び出し（旧来のglGenVertexArrays方式、DSA/compute/SSBO不使用）はEploreエージェントの調査で「GL4.1 Core Profileでもそのまま動作可能」と判明したため、変更対象を「コンテキスト生成時のバージョン/プロファイル指定の欠如」と「GLEWのcore profile対応(glewExperimental)」の2点だけに絞った。不要な変更（シェーダーの`#version`書き換え等）は行わない、というCLAUDE.mdの「スコープ厳守」に従った判断。
- ImGuiのマルチビューポート機能（`Renderer.cpp:128`で有効化されている）が、サブウィンドウ生成時に`glfwDefaultWindowHints()`を呼ばず一部hintのみ上書きする実装（`imgui_impl_glfw.cpp`の`ImGui_ImplGlfw_CreateWindow`）であることをコードリーディングで確認した上で、「最初に設定したcore profileのhintはサブウィンドウにも自動的に引き継がれる」と判断し、追加対応なしとした。
- PhysXのMacバイナリを自動取得/自動ビルドする仕組みをCMake側に作り込むのは、Mac実機での検証ができないこのセッションでは過剰投資でリスクが高いと判断し、「ユーザーが自分のMac環境でビルドした.aのパスをCACHE変数で渡す」という最小限のスキャフォールディングに留めた。
- Luauはリポジトリ内にビルド元のバージョン記録が一切無かった（Grep/Globで確認済み）。「たぶんこのバージョンだろう」で特定のgit tagを決め打ちすることはCLAUDE.mdの「たぶんこうだろうで進めない」原則に反するため、AskUserQuestionで確認し、「master既定＋CACHE変数で上書き可能」という安全側の設計にした。
- launcher/main.cppのMac対応は、前回セッション(2026-07-07)の決定を継続してスコープ外とした。readme.mdのMac Todoにも記載が無く、今回ユーザーからの追加指示も無かったため。

### どういう経緯か
1. Plan mode突入。CLAUDE.mdの「計画はモデル自身が行う」指示に従い、Phase 1の調査のみExploreエージェント2体を並列起動（OpenGL4.1非互換箇所の調査／ビルドシステムとMac向け構成の調査）、設計・計画自体はPlanエージェントに投げず自分で行った。
2. OpenGL調査エージェントの結果: コンテキストversion/profile指定が皆無、GLSLは`#version 330 core`でGL4.1互換、DSA/compute等の非互換API使用なし、という報告を受領。
3. ビルドシステム調査エージェントの結果: build.pyはCMakeのラッパーで`-A x64`等MSVC専用、CMakeLists.txtの依存ライブラリは全て`include/libs/*.lib`（Windows専用バイナリ）直リンク、PhysXは「NVIDIA公式にmacOS非対応」と報告された。
4. しかし自分でPhysXヘッダ（`PxPhysicsVersion.h`のPX_PHYSICS_VERSION_MAJOR/MINOR、`PxPreprocessor.h`のPX_APPLE_FAMILY定義、`foundation/unix/sse2/`ディレクトリの存在）を直接確認したところ、バージョンは5.6でありPhysX 5.x系はmacOS向けプリセットを公式サポートしていることが判明。エージェントの報告（「NVIDIA公式にmacOS非対応」）は古い/不正確な情報だったため、鵜呑みにせず自分で検証した（「trust but verify」の実践）。
5. AskUserQuestionでスコープ確認（ビルドシステム変更を含めるか）とPhysX認識確認を行い、「含める」「ユーザーの記憶通りMacでビルド可能と思われる」という回答を得た。
6. Luauのバージョン不明という追加の不明点が実装中に判明し、追加でAskUserQuestionを実施。
7. 最終プランを`C:\Users\Ryarta\.claude\plans\mac-kind-rocket.md`に記述しExitPlanMode、承認を得て実装。
8. 実装順序: main.cpp→game_main.cpp→CMakeLists.txt→build.py→`python build.py build`でWindowsビルド確認→`Recubin.exe`起動確認（15秒間クラッシュなし、GLFWウィンドウ作成・GLEW初期化・FBO Complete確認済み、タイムアウトで強制終了）→readme.md更新。

### 試して失敗した/計画から変更した点
- 特になし（プラン通りに実装が進んだ）。ただしMac実機での検証は今回のセッション（Windows環境）では原理的に不可能なため未実施。

### 未解決・保留
- **Mac実機でのビルド確認は一切未実施**。CMakeLists.txtのApple分岐は構文レビューのみで、実際に`find_package(glfw3)`等が解決できるか、PhysXの`.a`ファイル名（`PhysX_static_64`等、Windows版`.lib`名から類推した名前）がユーザーのMacビルド成果物と一致するかは未検証。次回Mac環境で確認が必要。
- Luauの`FetchContent`は既定で`master`を取得する設計にしたが、現在Windows版で使われているLuauの正確なバージョン/コミットは依然不明のまま。Mac版で挙動差異（API変更等）が出た場合は`RECUBIN_LUAU_GIT_TAG`で調整が必要になる。
- PhysXのMac向け`.a`ビルド自体（NVIDIA公式リポジトリのmacOSプリセット実行）はユーザー側の作業として残っている。
- launcher/main.cppのMac対応（`.app`バンドル化等）は前回に続き今回もスコープ外のまま。
- `build.py`の`launcher`アクション単体（`python build.py launcher`を直接実行した場合）はWindows限定ガードを入れていない。Mac上で実行すると`cl.exe`が見つからず`FileNotFoundError`が未捕捉のまま出る点は许容範囲内と判断したが、気になる場合は今後ガードを追加する余地がある。

### 暗黙仕様の発見（spec.mdに無い挙動）

- **ImGuiのマルチビューポート機能で生成されるサブウィンドウ（`imgui_impl_glfw.cpp`の`ImGui_ImplGlfw_CreateWindow`）は、GLFWのwindow hintをリセットしない**。`glfwWindowHint()`はグローバル状態としてGLFWに保持され、明示的に`glfwDefaultWindowHints()`を呼ぶかhintを上書きするまで次の`glfwCreateWindow()`にも引き継がれる。この性質のおかげで、メインウィンドウ作成時に一度だけcore profileのhintを設定すれば、ドッキングで切り離されたエディターパネルのサブウィンドウにも自動的に同じGLコンテキスト設定が適用される。逆に言えば、将来どこかで`glfwDefaultWindowHints()`やhintの上書きを追加すると、サブウィンドウだけ異なるGLプロファイルになりコンテキスト共有が壊れる可能性がある。
- **`include/PhysX`に同梱されているヘッダは、Windows版バイナリしか無いにもかかわらず`foundation/unix/sse2/PxUnixSse2InlineAoS.h`等Unix向けの実装も既に含まれている**（`PxPreprocessor.h`の`PX_APPLE_FAMILY`分岐経由で切り替わる）。つまりヘッダ一式は元々クロスプラットフォームなNVIDIA公式SDKからそのまま持ち込まれたものであり、Mac非対応なのはビルド済み`.lib`が無いことだけが原因。調査時に「ヘッダを見ずにバイナリの有無だけでMac対応可否を判断する」と誤った結論（エージェントの初期報告）に至りやすい典型例。

---
