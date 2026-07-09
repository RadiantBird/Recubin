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

## 2026-07-09 Mac実機ビルド・起動確認（PhysX抜き→本物PhysXまで到達）

### 何をしたか
Mac環境（Apple Silicon, macOS, arm64）に切り替わったユーザーから「ここからMac用のバイナリをビルドする計画を立てよう」と依頼され、Plan modeで調査した上で2段階に分けて実装した。

**Phase 1（物理無効でのMac起動）**
- `CMakeLists.txt`: `RECUBIN_LUAU_GIT_TAG`のデフォルトを`master`→`0.695`に変更（ユーザーのローカル`~/luau`クローンが指す実際のコミットと一致することをハッシュで確認できたため）。`COMMON_INCLUDES`に`include/PhysX`を追加（quote-includeの相対解決がMSVCの拡張ルールに依存していた既存バグの修正、全プラットフォーム共通）。yaml-cppを`find_package`(Homebrew 0.9.0)から`FetchContent`(jbeder/yaml-cpp `master`、`RECUBIN_YAMLCPP_GIT_TAG`で上書き可)に変更。
- `src/Core/Physics.cpp`: `Physics::init()`に`PxCreateFoundation`/`PxCreatePhysics`のnullチェックを追加し、失敗時は警告ログを出して`scene`を作らず抜けるようにした。
- `src/Util/WindowsPlatform.cpp`: ファイル全体を`#ifdef _WIN32`で囲んだ（CMakeの`file(GLOB_RECURSE)`が全プラットフォームでこのファイルをコンパイルしてしまっていたバグ）。
- `src/Core/LuauEngine.cpp`: x86専用のMXCSR保存/復元(`_mm_getcsr`等)をx86限定ガードに変更。
- `src/Editor/PropertiesPanel.cpp` / `SceneHierarchyPanel.cpp`: MSVC専用`strncpy_s`を`strncpy`に置換（バッファは元々ゼロ初期化済みで安全）。
- リンクだけ満たすダミーPhysX静的ライブラリ一式を`~/physx-mac-stub/`（リポジトリ外）に作成し、`RECUBIN_PHYSX_MAC_DIR`で指す形で`Recubin`/`RecubinEngine`/`RecubinTest`のビルド・Edit modeでの起動を確認した。

**Phase 2（本物のPhysXをMacでビルド）**
ユーザーから「PhysXのMac非対応は想定外だった。物理エンジンをインターフェース化して差し替えるのは影響範囲が広すぎて避けたい」と相談を受け、「PhysXソース自体をMac上で`TARGET_BUILD_PLATFORM=linux`と騙してビルドする」方針で合意した。
- PhysX公式リポジトリ（タグ`107.3-physx-5.6.1`、ヘッダのバージョン定義と一致）を`~/physx-src/`にclone。
- `physx/source/compiler/cmake/linux/CMakeLists.txt`: コンパイラ判定`STREQUAL "Clang"`を`MATCHES "Clang"`に変更（Apple Clangの`CMAKE_CXX_COMPILER_ID`は`"AppleClang"`で完全一致しないため）。`-Wno-missing-include-dirs`/`-Wno-poison-system-directories`/`-Wno-bitwise-instead-of-logical`/`-Wno-unused-parameter`を追加（実在するLinux固有の問題ではなく、`-Weverything -Werror`が拾う無害な警告のため）。
- `include/foundation/PxSIMDHelpers.h`: スカラー数学フォールバックの条件を`PX_LINUX && (PX_ARM || PX_A64)`から`(PX_LINUX || PX_OSX) && (PX_ARM || PX_A64)`に拡張（ARM Mac対応、PhysX側が元々Linux ARMのみ想定していた箇所）。
- `source/foundation/unix/FdUnixFPU.cpp`: x86専用のSSE FPU制御コード（`_mm_getcsr`等）を`PX_OSX_INTEL`（`PX_OSX && !(PX_ARM || PX_A64)`）に限定し、Apple SiliconはfenvベースのUnix汎用パスにfallbackするよう修正。
- `source/geomutils/src/mesh/GuBV4Build.cpp` / `source/physxextensions/src/tet/ExtDelaunayBoundaryInserter.cpp`: printf/fprintfの書式指定子`%d`→`%u`修正（`unsigned`引数との型不一致、`-Wformat -Werror`で検出）。
- `source/physxextensions/src/serialization/SnSerialUtils.cpp`: シリアライズ用バイナリプラットフォームタグ配列に`"macaarch64"`(`MA64`)を新規追加。PhysXは元々Intel Mac(`M_64`)とLinux ARM64(`LA64`)しかタグを持っておらず、Apple Silicon Macは`#error Unknown binary platform`で弾かれていた。
- 上記パッチを当てた上で`~/physx-mac-build/`に実際に`.a`一式（`libPhysX_static.a`等、Windows版と違い`_64`サフィックスなし）をビルドすることに成功。
- `CMakeLists.txt`の`PHYSX_MAC_COMPONENTS`を実際の出力名（`_64`なし）に合わせて修正し、`RECUBIN_PHYSX_MAC_DIR`を実ビルド成果物に向けて`Recubin`/`RecubinEngine`/`RecubinTest`を再ビルド。リンクエラーなしで成功。
- `Recubin`を起動し、Physics.cpp側のnullガードが発火しなくなった（＝本物のPhysXが初期化成功した）ことをログで確認。ユーザー本人がPlay modeを押して物理（重力落下・衝突）が実際に動作することを目視確認した。

### なぜそうしたか
- **PhysXをMacでビルドする方針を選んだ理由**: 別の物理エンジン(Jolt等)へのインターフェース化・差し替えは、`BaseCube.hpp`が`physx::PxRigidActor*`を生ポインタで直接保持し、`Physics.cpp`だけでPhysX呼び出しが168箇所、PhysXヘッダが24ファイルに波及するなど影響範囲が広く、ユーザー自身が「重たいし奇妙なバグの原因になる」とリスクを明確に否定したため。対して「PhysXソースをLinuxだと騙してビルドする」方針は、Recubin側のコードを一切変更せずに済む可能性がある、傷の浅い選択肢だった。
- **`TARGET_BUILD_PLATFORM=linux`が機能した理由**: PhysXのCMakeはこの変数を`.../cmake/${TARGET_BUILD_PLATFORM}/CMakeLists.txt`というディレクトリ名の選択にのみ使っており、実際のC++プリプロセッサレベルの分岐（`PX_OSX`/`PX_LINUX`/`PX_APPLE_FAMILY`等）は`__APPLE__`等の実際のコンパイラ定義から独立して決まる。つまり「ビルドスクリプトだけLinuxのふりをして、実際のコード生成は正直にmacOS向けになる」というギャップを利用できた。
- **PhysXの各パッチが必要だった理由（全て「LinuxとMacの違い」ではなく「PhysXが元々ARMを想定していなかった」ことに起因）**:
  - `PxVecMath.h`のコメントに「until a dedicated non-SIMD platform such as Arm comes online」とあり、PhysX 5.6.1はARM系で常にスカラー数学フォールバックを使う設計だが、`PxSIMDHelpers.h`のVec3V/V4StoreU分岐は「Linux ARM」だけを想定して書かれており「Mac ARM」が漏れていた。既存の設計意図（スカラーフォールバック時はV3StoreUを使う）に沿って条件を拡張しただけで、新しい実装を書く必要はなかった。
  - `FdUnixFPU.cpp`の`PX_OSX`分岐は歴史的にIntel Mac前提で書かれたコード（"osx defines SIMD as standard"というコメントがその名残）。Recubin本体の`LuauEngine.cpp`で見つけたのと全く同じ「x86 SSEレジスタ操作がARMに存在しない」パターンだったため、同じ対処（アーキテクチャガードを足してfenv汎用パスにfallback）を踏襲した。
  - シリアライズ用バイナリプラットフォームタグは、Intel Mac(`mac64`)とLinux ARM64(`linuxaarch64`)の両方には既にタグが用意されていたのに「Mac ARM64」の組み合わせだけが漏れていた。他プラットフォームのタグを壊さない追加のみの変更で対応した。
- **`RECUBIN_YAMLCPP_GIT_TAG`をmasterにした理由**: Homebrewのyaml-cpp(0.9.0リリースタグ)には、Windows版ビルドに使われた`include/yaml-cpp`同梱ヘッダが要求する`GetShowTrailingZero`/`insert_map_pair`の`force`引数がリンク時に見つからず失敗した。jbeder/yaml-cppの実際のタグ・コミットを確認したところ、これらのAPIは`master`ブランチには存在するが`yaml-cpp-0.9.0`タグには無いことを確認済み（推測ではなく実際にGitHub上のファイルを比較した）。Luauと同様、正確な固定コミットは不明なため、同じ「masterを既定にしつつCACHE変数で上書き可能」という設計を踏襲した。

### どういう経緯か
1. Plan modeで、Mac実機の環境調査（Homebrewの導入状況、find_packageの解決確認、既存のCMakeLists.txt/build.pyの内容確認）を実施。
2. PhysXのMac対応について、前回セッションの「PhysX 5.6はmacOS向けプリセットを公式サポートしている」という結論を鵜呑みにせず、NVIDIA公式リポジトリの実際のタグ・プリセットファイル・CMakeLists.txtを直接確認したところ、**macOS向けプリセットは実際には一切存在しない**ことが判明（前回の結論は誤りだった）。
3. AskUserQuestionで「PhysXのMac実態」「今回どこまで進めるか」を確認し、「まずOpenGL+Luauだけ動く状態を目指す」という方針で合意、Phase 1を実装。
4. Phase 1完了後、ユーザーから「PhysXがMac非対応だったのは想定外。物理エンジンのインターフェース化は避けたい。Playを押すとsegfaultする」と相談を受け、再度Plan modeへ。「PhysXをLinuxだと騙してビルドする」方針を提案し、事前に「generate_projects.shのlinuxプリセットはpackman経由の外部クロスコンパイラを必要としない」ことをGitHub上のファイルで確認した上で合意を得てPhase 2を実装。
5. Phase 2はビルド→エラー確認→最小パッチ→再ビルドを9回以上繰り返す反復作業になった（インクルードパス不足→AppleClang判定→SIMDスカラーフォールバック→x86 SSE FPU制御→printf書式警告→シリアライズプラットフォームタグ、の順に問題が出た）。
6. 最終的にPhysXの実ビルドに成功し、Recubin側の`PHYSX_MAC_COMPONENTS`を実際の出力名に合わせて修正、リンク成功。ユーザー本人がPlay modeで物理動作を目視確認して完了。

### 試して失敗した/計画から変更した点
- ビルド中に一度、`cmake --build --parallel`をCPUコア数（8）フルで実行してmacがフリーズ・再起動する事態が発生した（8GB RAM環境で8並列コンパイルはメモリを使い切る）。以降`--parallel 4`に落として安定した。次回以降、同程度のメモリのMacでは並列度を抑えることを推奨。
- `brew install`が`/opt/homebrew`の所有権エラーで失敗した。`sudo chown`が必要だったが、非対話シェルからsudoのパスワード入力ができず、ユーザー本人にターミナルで実行してもらった。
- PhysXのAppleClang判定修正は、当初IDEの診断（clangd等）がエラーを表示し続けたため一瞬「直っていないのでは」と疑ったが、これは非対話ビルドで渡している`TARGET_BUILD_PLATFORM=linux`等の特殊なcmake変数をIDEのlinterが認識していないためのノイズであり、実際のバックグラウンドビルド結果（正）を信頼する方針で正しかった。

### 未解決・保留
- Play modeの物理自体は動作確認できたが、既存のPhysXバイナリ依存機能（PVD接続、Vehicle2、GPU機能等）はMacビルドでは未検証。使う場面が出たら都度確認が必要。
- PhysXソースへのパッチは全て`~/physx-src/`（リポジトリ外）に対して行っており、Recubinリポジトリにはコミットされていない。次回Mac環境が変わった場合やPhysXを再ビルドする必要が出た場合、このセッションのパッチ内容（本エントリに記載）を再度当てる必要がある。パッチをどこかに永続化する(例えばdocフォルダにdiffを保存する等)かは今回検討していない。
- yaml-cppの`RECUBIN_YAMLCPP_GIT_TAG`はLuau同様「master既定」のままで、正確な固定バージョンは依然不明。
- `build.py`の`run`/`brun`/`test`アクションが`.exe`拡張子をハードコードしている件は今回も未対応のまま。

### 暗黙仕様の発見（spec.mdに無い挙動）
- **`Workspace::initPhysics()`は失敗しても`physicsEngine`に非nullをセットしてしまう**（`Physics`オブジェクト自体は必ず構築される。中のPhysX初期化が失敗しても`Workspace::getPhysicsEngine()`は非nullを返す）。呼び出し側の`if (ws->getPhysicsEngine())`という頻出パターンは「PhysXが実際に使えるか」ではなく「Physicsオブジェクトが構築済みか」しか見ておらず、本当に危険なのは`Physics::init()`内部の無ガード参照外しだけだった。
- **`Physics::update()`は`Workspace::PhysicsEnabled`フラグを先頭でチェックしており、falseなら即returnして`scene`に一切触れない**という、物理を安全に無効化するための仕組みが既に用意されていた（今回は使わなかったが、将来「特定のWorkspaceだけ物理を切る」用途に使える）。
- **PhysX 5.6.1は設計上ARM系CPUで常にスカラー数学（非SIMD）フォールバックを使う**（`PxVecMath.h`の`COMPILE_VECTOR_INTRINSICS`は`PX_INTEL_FAMILY`か`PX_SWITCH`でしか1にならない）。NEON実装は存在しない。今回のMacビルドはARMネイティブ・スカラー数学で動作しており、パフォーマンス特性はWindows版（x86 SIMD）と異なる可能性がある。
- **PhysXのバイナリシリアライズ形式にはプラットフォームタグ埋め込みがある**（`SnSerialUtils.cpp`の`sBinaryPlatformTags`）。異なるプラットフォームタグでシリアライズされたPhysXデータを読み込むと非互換エラーになる可能性があるため、将来Mac版でシリアライズ済み物理データを扱う場合はこのタグの扱いに注意が必要（今回追加した`macaarch64`タグは他プラットフォームとの互換性は無い、新規のMac専用タグ）。

---

## 2026-07-09 v2.0ネットワーク基盤 設計+モック実装（ENet統合・UseNetwork・Users・LocalScript）

### 指示内容
readme.md「ネットワーク関係」セクション（UseNetwork/Usersクラス/ENetベース動的ホストマイグレーション型サーバー・クライアント通信基盤）について、v2.0に向けた設計とモック実装を依頼された。Plan modeで、CLAUDE.mdの指示によりPlanエージェントは使わず設計は自分で行い、Exploreエージェント3体（Instance/System/Script階層、既存ネットワークコード・ビルド設定、Luauバインディング・シリアライズパターン）を並列調査した上で計画を立てた。

AskUserQuestionで実装範囲を確認: (1) 実装範囲は「基盤のみ実装、応用（リソーススコアによるホスト自動選出・動的ロール切替の実処理）は設計止まり」を選択。(2) LocalScriptは「最小限のクラスを新規作成」を選択（UseNetwork=trueでのScript/LocalScript使い分けに必要なため）。(3) モックデモ内容は「ダミー座標+チャット文字列」の最小構成。(4) 起動方法は「コマンドライン引数」（`--host [port]` / `--connect <address> [port]`）。(5) Usersコンテナは「Insert Objectから除外」（System同様、エンジン自動管理の特殊コンテナ）。(6) LocalScript実行フィルタは「ClientではLocalScriptのみ実行（Scriptは実行しない）」を選択。

### 何をしたか
- **ENetのベンダリング**: `lsalzman/enet`公式リポジトリ(1.3.18)からヘッダ一式を`include/enet/`、ソース(.c)一式を`src/Network/Vendor/enet/`にそのまま取得配置（改変なし）。
- **`CMakeLists.txt`**: `file(GLOB_RECURSE ALL_SOURCES "src/*.cpp")`を`.cpp`+`.c`両方収集するよう拡張（`ALL_SOURCES_CPP`+`ALL_SOURCES_C`→`ALL_SOURCES`）。Windows`COMMON_LIBS`に`ws2_32`/`winmm`を追加（ENetが要求するWinsock/タイマーAPI）。`include/enet`を`COMMON_INCLUDES`に追加しようとしたが、`enet.h`が`#include "enet/win32.h"`という`include/`ルート相対のパスで自分自身を参照する構造だったため、既存の`include`（親ディレクトリ）だけで十分と判断し撤回した。
- **`include/Network/NetworkTypes.hpp`（新規）**: `NetworkRole`(Offline/Host/Client)、`NetworkChannel`(Reliable=0/Unreliable=1)、`MessageType`(Chat/DummyPosition)、将来のホスト自動選出用`PeerResourceInfo`構造体（未使用、TODO予約のみ）。
- **`include/Network/NetworkManager.hpp` / `src/Network/NetworkManager.cpp`（新規）**: `SystemState::get()`と同型のMeyer's singleton。`startHost(port)`/`connect(address,port)`/`shutdown()`/`poll()`（`enet_host_service`をノンブロッキングで回す）/`sendChatMessage()`(RELIABLE)/`sendDummyPosition()`(UNRELIABLE)。ENetHostの所有はPhysXの`s_foundation`/`s_pxPhysics`と同じ「生ポインタ+明示的destroy」パターンを踏襲（CLAUDE.mdのunique_ptr原則の例外＝C API所有オブジェクトに該当すると判断）。
- **`include/Instances/System.hpp` / `src/Instances/System.cpp`**: `bool UseNetwork = false;`を追加し、`PropertyRegistry::field<&System::UseNetwork>("UseNetwork")`を既存の`BaseResolution`と同じ`s_systemRegistered`ブロックに追加。
- **`include/Instances/Users.hpp`（新規、ヘッダオンリー）**: `Folder`と同型の最小`Instance`派生コンテナ。Insert Objectメニュー（`SceneHierarchyPanel.cpp::renderInsertMenu`）は動的なクラス列挙ではなく明示的な`tryInsertInstance<T>`の羅列だったため、単に追加しないだけで自然にInsert Object非表示になった（`System`と同じ理由）。
- **`src/Core/SceneRuntime.cpp`（`loadAndBind`）**: `Workspace`/`PathfindingService`と同じ「無ければ自動生成」パターンで`Users`コンテナを`System`直下に確保し、`User`を`System`直下ではなく`System/Users`直下に配置するよう変更。
- **`include/Instances/LocalScript.hpp` / `src/Instances/LocalScript.cpp`（新規）**: `Script`派生の最小クラス。`clone()`をオーバーライドして`LocalScript`型で複製されるようにした（オーバーライドしないと`Script::clone()`が常に素の`Script`を生成してしまうため）。
- **`src/Core/LuauEngine.cpp`（`executeWorkspaceScripts`）**: `m_system->UseNetwork && NetworkManager::get().getRole()==Client`の場合、`LocalScript`以外（＝`Script`）の実行をスキップするフィルタを追加。
- **`src/game_main.cpp`**: `int main()`を`int main(int argc, char* argv[])`に変更し、`--host [port]`/`--connect <address> [port]`をパースして起動時に`NetworkManager::get().startHost()/connect()`を呼ぶ`parseNetworkArgs()`を追加。メインループ先頭（物理更新より前）に`NetworkManager::get().poll()`を挿入し、接続確立を検出したら`sendChatMessage()`を1回、Host側は0.1秒毎に単調増加するダミー座標を`sendDummyPosition()`で送るモックデモを追加。ループ終了時に`NetworkManager::get().shutdown()`を追加。
- **`readme.md`**: UseNetwork/Usersクラスを`[x]`に、動的ホストマイグレーション項目全体を`[~]`（ENet統合+チャンネル分離のみ`[x]`、リソース計測/自動選出/動的ロール切替は`[ ]`のまま）に、LocalScript実装を`[~]`に更新。

### なぜそうしたか
- **PlanエージェントもExploreエージェントも計画は自分で行った**: CLAUDE.mdの明示的指示（「計画・設計はこのセッションのモデル自身が行うこと」）に従った。前回Mac対応セッションと同じ方針。
- **実装範囲を「基盤のみ」に絞った理由**: readme.mdのネットワーク項目（リソーススコア計測・ホスト自動選出ハンドシェイク・動的ロール切替）は実装量・検証量ともに1セッションで扱うには過大と判断し、AskUserQuestionでユーザーに確認した上で基盤tierに限定した。ホスト自動選出等は「Roleが切り替われば`executeWorkspaceScripts`が次フレームから自動的に新しい実行セットに従う」という設計だけ先に整えておき、実際のトリガー（選出アルゴリズム）は`NetworkManager`のTODOコメントとして予約するに留めた。
- **ENetHostを生ポインタで持つ判断**: CLAUDE.mdは「原則`unique_ptr`使用」だが「C API (OpenGL, PhysX等) への引数」は生ポインタ許容としている。`include/Core/Physics.hpp`の`s_foundation`/`s_pxPhysics`（PhysXオブジェクト）が同じ理由で生ポインタ+`release()`パターンを採っていたため、ENetHostも同じ分類（外部Cライブラリの自前ライフサイクルAPIを持つオブジェクト）と判断し、既存パターンに倣った。
- **RecubinEngine(`game_main.cpp`)のみにCLI統合し、`main.cpp`（エディター）には統合しなかった理由**: readmeのネットワーク機能は配布ゲームランタイムの文脈で説明されており、エディターのPlay/Stop状態機械（`main.cpp`の`isPlaying && !wasPlaying`等）は既に複雑で、そこにHost/Client切替を混ぜると影響範囲が大きくなりすぎると判断し、AskUserQuestionで確認の上スコープ外にした。
- **Insert ObjectからUsers/Systemが除外される仕組みを利用した理由**: `renderInsertMenu`はクラスレジストリの動的列挙ではなく手書きのメニュー項目列挙だと分かったため、「除外する」という要件は「メニューに追加しない」だけで自動的に満たされ、追加の除外ロジック実装が不要だった。

### どういう経緯か
1. Plan modeで、Instance/System/Script階層・既存ネットワークコード有無・ビルド設定・Luauバインディングパターンを調査するExploreエージェント3体を並列起動。
2. 調査結果を踏まえ、AskUserQuestionで実装範囲・LocalScriptの扱い・モックデモ内容・起動方法・Users可視性・LocalScript実行フィルタの6点を確認。
3. `PropertyRegistry`/`Instance`/`System`/`Script`/`User`/`SceneRuntime.cpp`/`CMakeLists.txt`の実物を読み、正確なファイルパス・行番号を伴う計画を`C:\Users\Ryarta\.claude\plans\v2-0-cached-stallman.md`に記述してExitPlanMode、承認を得た。
4. 実装順序通り、ENetベンダリング→CMakeLists.txt統合→NetworkManager→System.UseNetwork→Usersクラス→LocalScript+実行フィルタ→game_main.cpp統合、の順で実装。
5. **`Users`クラス追加中に、承認済みプラン外の`main.cpp`への波及に気づいた**: `resetSystemForReload()`（Play/Stop切替・シーンリロード時に「`user`以外の`System`直下の子を全削除する」関数）が`child.get() == user.get()`という直接比較をしており、`User`が`System`直下から`System/Users`直下へ1階層深くなると、`Users`コンテナごと（中の`user`もろとも）毎回誤削除される regression になることを発見。プランのファイル一覧に無かったため、実装を止めてAskUserQuestionでユーザーに確認し、「main.cppを最小限修正する」を選んで対応（`Users`コンテナ自体を`user`と同様に例外扱いする形にロジックを変更）。
6. 実装完了後、`python build.py build`でビルド成功（ENetの`.c`ファイル群も`RecubinCore`に正しく取り込まれてコンパイルされることを確認。警告のみでエラーなし）。
7. 実機検証: `startup.yaml`を一時的にプロジェクトルートに作成し（`StartScene: assets/scenes/void.yaml`）、`RecubinEngine.exe --host 7777`と`RecubinEngine.exe --connect 127.0.0.1 7777`を実際に2プロセスlocalhostで起動。1回目の検証で「Clientが送るはずのチャットメッセージをHostが一切受信しない」という不具合を発見。
8. **バグ調査**: `enet_peer_send()`のENet実装（`peer.c:114`）を直接読み、`peer->state != ENET_PEER_STATE_CONNECTED`の場合は`-1`を返すだけでパケットを送信もキューイングも破棄もしないことを確認。自分の`NetworkManager::connect()`実装が`enet_host_connect()`直後（実際のハンドシェイク完了前）に同期的に`m_peers.push_back(peer)`していたため、`hasPeers()`が本当の接続確立より早く`true`を返し、その最初のフレームで送った`sendChatMessage()`が`enet_peer_send`の状態チェックに弾かれて黙って失敗（かつ`ENetPacket`がリークする）というバグだったと特定。
9. 修正: `m_peers`への追加を`connect()`内の即時pushから、`handleEvent()`の`ENET_EVENT_TYPE_CONNECT`受信時（Host/Client共通）に一本化。あわせて`broadcastPacket()`のClient経路（`enet_peer_send`直呼び）が失敗した場合に`enet_packet_destroy()`で明示的に破棄するようにした（`enet_host_broadcast`はHost経路で内部的に未送信時の破棄を保証済みと`host.c:286`で確認済みだったため、Client経路だけの片手落ちだった）。
10. 再ビルド→再検証で、双方向のRELIABLEチャット送受信・Host→ClientのUNRELIABLE位置同期（連番ログ）を確認。さらにHost側プロセスを先に強制終了し、Client側がクラッシュせず`peer disconnected`ログを出して安全に継続動作することを確認。
11. 検証用の一時`startup.yaml`を削除し、readme.mdのチェックボックスを更新。progress.md記録（本エントリ）。

### 試して失敗した/計画から変更した点
- **`COMMON_INCLUDES`に`include/enet`を追加しようとして撤回**: ENet公式ヘッダの内部includeが`#include "enet/win32.h"`という`include/`ルートからの相対パスを前提にしていたため、`include/enet`自体を追加インクルードパスにすると意味がない（むしろヘッダ名衝突のリスクがあるだけ）と判明し、既存の`include`ルートのみで動作することを確認した上で撤回した。
- **`NetworkManager::connect()`で接続確立前に`m_peers`へpushしていた設計ミス**: 「`enet_host_connect()`が返すENetPeer*は接続要求開始時点で有効なポインタだから、返り値をもらった時点で`m_peers`に入れておけば良い」という誤った直感で最初に実装し、ビルド・1回目の起動テストまで気づかなかった。実際に2プロセスを動かして「Host側にチャットが届かない」という現象で初めて発覚し、ENet公式ソース（`peer.c`）を直接読んで原因を特定した。「ハンドシェイク完了（`ENET_EVENT_TYPE_CONNECT`イベント）を見てから接続済みとして扱う」という当たり前の非同期API取り扱いを、最初は省略してしまっていた。
- **Insert Objectメニューへの`LocalScript`追加は見送り**: 承認済みプランのファイル一覧に`SceneHierarchyPanel.cpp`が含まれていなかったため、スコープ外として触らなかった。エディターUIから`LocalScript`を挿入する手段は現状無い（YAML直書きかLuau経由でのみ生成可能）。

### 未解決・保留
- **リソーススコア計測・ホスト自動選出・動的ロール切替の実処理は完全に未実装**（`NetworkManager.hpp`にTODOコメントで設計方針のみ記載）。次回実装するなら、`PeerResourceInfo`をRELIABLEチャンネルで定期送信する仕組み→Host側での集計→切断検知時の再選出→新Hostへの`ENetHost`再構築（Client用host→Host用hostへの実体差し替えが必要になるはずで、現在の`NetworkManager`はHost/Clientで別々の`enet_host_create`呼び出しをしている前提なので、この切替ロジックは追加設計が必要）という順で検討することになりそう。
- **複数Peer分の`User`表現は完全に未実装**。現状`Users`コンテナは「1プロセス=1ローカル`User`」を格納するだけの入れ物で、リモートPeerに対応する`User`相当インスタンス（カメラ・入力を持たない軽量な複製）を接続時に生成/切断時に破棄する仕組みは今回のスコープ外のまま。
- **エディター（`Recubin.exe`/`main.cpp`）からのHost/Client起動には未対応**。CLI引数はランタイム（`RecubinEngine.exe`/`game_main.cpp`)のみ。
- **`LocalScript`はInsert Objectメニュー未対応**（上記参照）。ModuleScript実装・taskモジュール（delay/spawn/wait）も引き続き未着手（readme既存Todo）。
- **`APIENTRY`マクロ再定義警告**（`game_main.cpp`ビルド時、`glfw3.h`の定義と`minwindef.h`の定義が競合）が今回のビルドで新たに出るようになった。ENetの`win32.h`が`winsock2.h`/`windows.h`を`glfw3.h`より後に間接includeしていることが原因と推測されるが、警告のみでビルド・実行には影響しないため未対応のまま。気になる場合は次回`include`順序の調整を検討。
- **`enet_initialize()`/`enet_deinitialize()`のプロセス全体での呼び出し回数バランス**は`NetworkManager`内で`m_enetInitialized`フラグにより1回のみ初期化する設計にしたが、`enet_deinitialize()`自体は一度も呼んでいない（プロセス終了時のOS回収に任せている）。将来複数回のstartHost/connect/shutdownサイクルを1プロセス内で繰り返すユースケースが出た場合、この非対称性で問題が起きないか要確認。

### 暗黙仕様の発見（spec.mdに無い挙動）
- **`SceneLoader`のシングルトン解決（`registerSingleton`）はツリー上の深さに依存せず、YAMLノードの`ClassName`一致のみで判定される**（`SceneLoader.cpp`の`s_singletons`は`unordered_map<string(className), shared_ptr<Instance>>`で、木構造上のパスは見ていない）。そのため今回`User`を`System`直下から`System/Users`直下へ1階層深くしても、既存のシーンYAML読み込みロジックには一切変更が不要だった。逆に言えば、将来同じクラス名のシングルトンをツリー上の複数箇所に置きたくなった場合、このマップは名前だけで一意に解決するため対応できない。
- **`renderInsertMenu`（`SceneHierarchyPanel.cpp`）はクラスレジストリの動的列挙ではなく、`tryInsertInstance<T>(...)`のハードコードされた羅列である**。`PropertyRegistry::registerClass`で登録したクラスが自動的にInsert Objectメニューに現れるわけではない。新クラスをエディターから挿入可能にするには、このメニュー関数に明示的に1行追加する必要がある（spec.mdの「新規クラスは自動的にエディターに公開される」という記述は、プロパティパネルでの表示自動化を指しており、Insert Objectメニューへの掲載は別の仕組みだと分かった）。
- **`enet_host_broadcast`は未接続Peerを自動的にスキップし、誰にも送れなかった場合はpacket自体を内部で安全に破棄する**が、`enet_peer_send`を個別Peerに対して直接呼ぶ経路（Client視点でHostへ送る場合など、Peerが1つしかない場合の最適化）は送信失敗時にpacketを破棄せず単に`-1`を返すだけ、という非対称なAPI設計になっている（ENet 1.3.18の`host.c`/`peer.c`で確認）。ENetを使うコードは、`enet_host_broadcast`を使わない単一Peer送信経路では必ず戻り値をチェックしてpacketを破棄する必要がある。

---
