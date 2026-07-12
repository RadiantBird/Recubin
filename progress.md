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

## 2026-07-10 Users無限増殖バグ・着席時のAttachment/Motorずれバグ修正

### 指示内容
readme.mdのTodoにある2件の未修正バグの修正を依頼された。(1)「無限増殖バグ」: `System/Users`がPlay/Stop・Loadのたびに`Users`→`Users1`→`Users11`…とキー衝突リネームが連鎖し増殖する。(2)「Attachmentは、アセンブリ剛体によって座標がずれ、不安定になる」: 車両（複数Cubeのweldアセンブリ）にキャラクターが着席すると、Attachment/Motor（車輪等）の座標がずれる。Plan modeでExploreエージェント2体（並列）に読み取り専用調査をさせ、計画は自分で行った。

### 何をしたか
1. **Users無限増殖の修正**（`src/Core/SceneRuntime.cpp`の`loadAndBind`のみ）: `loadScene()`を呼ぶ前に`system->children`から既存の`Users`コンテナを探し、見つかれば`System`/`User`と同様に`SceneLoader::registerSingleton("Users", usersContainer)`で事前登録するようにした。
2. **着席時のSeat/Attachmentずれ（1回目・不十分だった修正）**: `Humanoid::sitOn()`/`standUp()`（`src/Instances/Humanoid.cpp`）で、着席中はRootの転倒防止用`LockFlags`(`eLOCK_ANGULAR_X|Z`)を解除し、`standUp()`のWeld除去前に復帰させるようにした。
3. **Motor::Axisのセマンティクス変更**: `include/Instances/Motor.hpp`の`Axis`コメントを「ワールド方向」→「Cube0基準のローカル方向」に変更し、`src/Core/Physics.cpp`の`createMotor()`で`motor->Axis`を`sp0->getWorldCFrame().Rotation.rotate(motor->Axis)`でCube0の現在の回転を通してからワールド軸を作るよう変更。
4. **真の原因への対処（座標ずれの本体）**: `Humanoid::sitOn()`で、`m_seatWeld->setCube1(seat)`の直後に`physics->createWeld(m_seatWeld, workspace)`を同フレーム内で同期的に呼び出すよう変更。Rootをスナップした瞬間の姿勢でcompoundを確定させ、次フレームの`Physics::update()`まで処理が遅延することで生じる「Rootだけ独立した自由な剛体のまま物理シミュレーションが1ステップ以上進んでしまう空白窓」を無くした。
5. **二重処理の除去**: 上記4の同期呼び出し後、`setCube1()`内部の`registerIfReady()`が既に`workspace.pendingConstraints`へ積んでいた同じWeldを`std::remove`+`erase`で取り除き、次フレームの`Physics::update()`が同じWeldに対し`createWeld`を再度呼んで車輪Motorまで無駄に二重再構築してしまうのを防いだ。

### なぜそうしたか
- **Users修正を`SceneRuntime.cpp`のみに絞った理由**: `main.cpp`の`resetSystemForReload()`は既にPlay/Stop時に既存の`Users`コンテナ（と`user`）を意図的に温存する設計だった。温存されているのに`loadScene()`側がそれを知らず`SceneLoader::createInstance("Users")`で新規生成・`addChild`してしまうのが衝突の原因だったため、`System`/`User`と同じ「事前シングルトン登録でマージさせる」パターンに揃えるだけで、他ファイルを触らずに解決できた。
- **LockFlags解除だけでは足りなかった理由**: ユーザーからの実機フィードバックで「回転ロックが原因ではなかった」と判明。ロック自体は「車両が傾いた状態でのcompound全体ロック」という別の実在する問題ではあったが、今回の座標ずれの主因ではなかった。この経験から、以降は静的なコードリーディングによる仮説だけで確定せず、実機ログでの検証を挟む方針に切り替えた。
- **Motor::Axisをローカル方向に変えた理由**: ユーザーに「Axisの意味をCube0基準のローカル方向に変える」か「リビルド時に直前のワールド軸を維持する」かをAskUserQuestionで確認し、前者（意味変更）を選択された。既存シーンでワールド軸を前提にAxis値を手動調整していた場合は挙動が変わりうるトレードオフだが、根本的に自然な挙動になる利点を優先。
- **診断ログを一時的に仕込んだ理由**: 2回連続で立てた仮説（回転ロック起因、Motor::Axisのワールド固定起因）が外れた・部分的にしか当たらなかったため、これ以上の推測は避け`Humanoid::sitOn()`と`Physics::rebuildGroup()`に一時ログ（`[SIT-DEBUG]`/`[REBUILD-DEBUG]`）を仕込みユーザーに実機再現→ログ提供してもらった。ログから「スナップ直後のRoot座標」と「実際にcompoundを組む瞬間のRoot座標」が一致していない（1フレーム分ズレて動いている）ことが直接判明し、これが決め手になった。
- **`Physics::createWeld`を同期呼びにした理由**: `Physics::update()`の内部順序が「①`scene->simulate`/`fetchResults`で物理を1ステップ以上進める→②`workspace.pendingConstraints`を処理してWeld/Motorを生成」（`Physics.cpp`の`update()`本体）であることをコードで確認。`sitOn()`はメインループの`processInput`（`physicsEngine->update()`より後）から呼ばれるため、`sitOn()`内でWeldを登録するだけだと、次フレームの①が完了した後の②でようやくcompound化される。その間Rootは独立した自由な動的剛体のままで、①によって動いてしまった分がそのままアセンブリの原点として焼き付く。`Physics::createWeld`が`public`メソッドで`sitOn()`から直接呼べたため、既存の非同期キュー処理に手を入れず、呼び出し側で「今すぐ確定させる」という最小限の変更で空白窓を消せた。
- **pendingConstraintsから明示的に除去した理由**: 同期呼び出し後もキューに残った同じWeldが次フレームに再度`createWeld`されると、`rebuildGroup`末尾の「assembly内のcubeを参照しているMotorを再構築」処理（`Physics.cpp`）が車輪Motor全部を無駄にもう一度作り直す。座標のズレという意味では二重処理後も理論上は自己無矛盾（Motor::Axis修正済みのため）だが、ユーザーが「細かい再構築の順番ずれ」を懸念した通り不要な処理であり、キューから取り除いて二重実行自体を無くす方が素直で安全と判断した。

### どういう経緯か
1. Plan modeでExploreエージェント2体（アタッチメント/Weld/Physics関連の調査、Users増殖バグのInstance/SceneLoader関連の調査）を並列起動し、原因箇所を特定した計画を`C:\Users\Ryarta\.claude\plans\snug-fluttering-peacock.md`に記述。当初「Weldを別アクター+PxJointにしてcompound分離する」案も検討したが、ユーザーから「回転ロックがあると車両がどうやっても転倒しなくなるのでは」と指摘され、joint方式・compound方式どちらでも同じ問題が残ることに気づき、「着席中だけRootのLockFlagsを外す」案に修正して承認を得た。
2. Users修正（`SceneRuntime.cpp`）とLockFlags解除修正（`Humanoid.cpp`）を実装、ビルド成功、ユーザーに実機確認を依頼。
3. ユーザーから「アタッチメントのずれは回転ロックが原因ではなかった」と否定的フィードバック。`Spatial::getWorldCFrame`/`BaseCube::syncPhysics`/`CFrame::operator*`/`Quaternion`の数式を全て読み直したが理論上のバグは見つからず、AskUserQuestionで症状を絞り込み（着席した瞬間から発生／車輪側がずれ、モーターの回転軸が変わる感じ）。
4. `Weld::collectAssembly`→`Physics::rebuildGroup`のMotor再構築処理を精読し、`Motor::Axis`が「ワールド方向」固定である点を発見。Weldリビルド（着席で巻き添えになる）のたびに車体の傾きを無視した軸で再生成される、という仮説を立て、AskUserQuestionで修正方針（Axisの意味変更 vs リビルド時の軸継承）を確認して前者を実装。
5. ユーザーから実際のシーン（`assets/scenes/physics_test.yaml`）と、着席済み状態のスクリーンショット2枚が提供され、「無人時は揃っているが、着席した溶接後にずれる。座席から遠い状態で溶接されるとずれが大きくなる」という追加情報を得た。物理挙動自体に影響するかをAskUserQuestionで確認し「実際の物理もおかしい」と判明。
6. 「距離に比例してずれる」という手がかりから、原点の回転誤差が遠方ほど増幅される、という仮説を立てたが確証が持てず、`Humanoid::sitOn()`と`Physics::rebuildGroup()`に一時診断ログを追加してビルド、ユーザーに実機再現とログ提供を依頼。
7. 提供された`output.txt`（UTF-16のため一部文字化けして見えるが内容は読み取れた）から、`[SIT-DEBUG]`のRoot座標と`[REBUILD-DEBUG] origin=Root`のRoot座標が別フレームで一致しないことを確認し、「①物理シミュレーション→②Weld生成」という`Physics::update()`内の順序が原因と特定。`sitOn()`内で`physics->createWeld()`を同期的に呼ぶ修正を実装、診断ログは削除、ビルド成功。ユーザーが実機確認し「座席ずれは直った」が「まだバグがある」「細かい再構築の順番ずれでは」とのフィードバック。
8. 同期呼び出し後も`pendingConstraints`に同じWeldが残存し次フレームで二重にMotorまで再構築されている点に気づき、`std::remove`+`erase`で明示的にキューから除去する修正を追加、ビルド成功。ユーザーが実機確認し「直りました」と確認を得た。

### 試して失敗した/計画から変更した点
- **1回目の修正（LockFlags解除）は的外れではなかったが不十分だった**: 車両が傾いた状態でのcompound全体ロックという実在の問題ではあるものの、ユーザーが報告した座標ずれの主因ではなかった。むしろLockFlagsを解除したことで、後に発見した「1フレームの空白窓」の間Rootが完全に無拘束になり、症状を悪化させていた可能性がある（空白窓自体を無くす4番目の修正で解消済み）。
- **RootとSeatを別アクター+`PxJoint`（Motorと同じ仕組み）でcompound分離する案は不採用**: ユーザー指摘の通り、Rootに角度ロックがある限りjoint経由でも車両全体の転倒を妨げる副作用が残ると判断し、計画段階で「着席中だけLockFlagsを外す」案に切り替えた。
- **「AttachmentのworldCFrame計算式自体にバグがある」という仮説は数式検証の結果否定**: `Spatial::getWorldCFrame`/`BaseCube::syncPhysics`/`CFrame::operator*`/`Quaternion::rotate`/`conjugate`を全て手計算で検算し、数式レベルでは矛盾がないことを確認した。この「静的読解だけでは埒が明かない」経験から、診断ログ+実機再現に切り替える判断につながった。
- **診断ログの追加は`RCBN_LOG`の重い文字列結合処理をホットパス（`rebuildGroup`のループ内）に一時的に入れたため、フレームレートが低下し「1フレームの空白窓」で車両が移動する距離が普段より誇大化していた可能性がある**（ログ自体は削除済みだが、次回同様の診断をする際はこの副作用に留意）。

### 未解決・保留
- 今回発見した「compound全体LockFlagsのOR合成（`Physics::rebuildGroup`）」自体の設計（`Physics.cpp`のコメントにある通り、Weld合成のたびに角度ロックを失わないための意図的な仕様）は変更していない。着席中はRootのLockFlagsを`0`にすることで実質的にこの合成対象から外しているが、将来「傾いた車両」以外の別のロック持ちCube（Humanoid以外）がアセンブリに加わるケースで同種の振動が再発する可能性は残る。
- Users増殖バグは今回のセッション開始前から`assets/scenes/physics_test.yaml`に既に`Users`/`Users1`/`Users11`/`Users111`という壊れたデータが残存している（本セッション中にユーザーが貼ったYAML内容で確認済み）。今回の修正は「今後新たに増殖しない」ようにするものであり、既存の壊れたシーンファイル自体のクリーンアップ（重複`Users`ノードの手動除去）は未対応のまま。
- `Physics::createWeld`を`sitOn()`から同期的に呼ぶパターンは、他の「ランタイム中に動的にWeldを追加する」既存コード（あれば）でも同じ1フレーム空白窓の問題を抱えている可能性がある。今回はSeatWeldのみ対応し、他の動的Weld生成箇所の横展開調査はしていない。

### 暗黙仕様の発見（spec.mdに無い挙動）
- **`Physics::update()`内の処理順序は「物理シミュレーションが先、新規Weld/Motor/Rope/Rodの生成処理が後」**（`Physics.cpp`の`update()`本体、`scene->simulate`/`fetchResults`のループが先頭、`workspace.pendingConstraints`処理がその後）。そのため、あるフレームで`Workspace::registerConstraint`経由で新しいWeldを登録しても、それが実際にcompound化されるのは早くて「次の`Physics::update()`呼び出しの、そのフレームの物理シミュレーション完了後」になる。ランタイム中に動的生成したCubeを即座に他のCubeと剛体一体化させたい場合、この非同期キューに任せると最低1フレーム分「まだ独立した自由な剛体」の状態で物理シミュレーションを受けてしまう。即座に一体化させたい場合は`Physics::createWeld()`（`public`）を直接同期呼びする必要がある（今回`Humanoid::sitOn()`で採用したパターン）。
- **`Physics::createWeld`/`createMotor`は同じWeld/Motorに対して複数回呼んでも安全（べき等ではないが破壊的ではない）**: `createWeld`内の`alreadyRegistered`チェックにより`m_constraints`への二重登録は防がれ、`rebuildGroup`は毎回一貫した現在位置から再構築するため、二重に呼んでも座標が壊れることはない（ただし車輪Motor等の巻き添え再構築が無駄に倍増するため、パフォーマンス・ログノイズの観点では避けるべき）。
- **`Weld::collectAssembly`が発見する`allWelds`/`allMotors`は、開始Cubeの所属Workspaceを`root`引数として渡した際、そのWorkspace配下の子孫すべてを毎回スキャンして集める**（`Weld.cpp`の`collect`ラムダによる全走査）ため、Workspace内のWeld/Motor総数に比例したコストがかかる。頻繁に動的Weldを生成・破棄するようなユースケース（今回のSeatWeldのように着席のたびに生成される場合）では、Workspace内のWeld/Motor数が多いシーンほど`sitOn()`のコストが増える可能性がある。

---

## 2026-07-11 カスタムメッシュUV自動生成 + UV空間Decal配置（xatlas統合）

### 指示内容
readme.mdのTodo「カスタムメッシュにUVを自動生成したい。これが実現すればどこでもデカールやテクスチャーが貼れるようになる」について、まず技術的feasibilityを検討し、可能なら計画するよう依頼された。Plan modeでExploreエージェントを複数ラウンド（メッシュ/UV構造、Decal/テクスチャ投影の仕組み、spec.md確認、Decal配置・レイキャスト機構、PropertyRegistry/シリアライズ）並列調査した上で、AskUserQuestionを3ラウンド実施:
1. スコープ（UV自動生成+全体テクスチャのみ vs クリックした特定箇所へのDecal個別配置まで含む）→**Decal個別配置まで含める**を選択。
2. xatlas（MIT、UV自動展開ライブラリ）新規導入の可否（ビルドシステム変更を伴う前提で確認）→**導入許可**。
3. UV生成タイミング（GLBインポート時に自動判定 vs エディタの手動ボタンのみ）→**インポート時自動判定**。
4. Decalサイズ単位（UV空間0-1で直接指定 vs ワールド単位からの近似変換）→**UV空間で直接指定**。
5. 同時表示Decal数上限（4/8/16）→**8個**。
6. 「UV再生成」手動ボタンのUndo要否 →**Undoなし、確認ポップアップのみ**。
7. 「Decal配置モード」でクリック後の挙動（1個で自動OFF vs 手動OFFまで連続配置可）→**1個で自動OFF**。

最終的に、詳細実装設計をPlanエージェントに1回委譲して精査した上で計画ファイル(`C:\Users\Ryarta\.claude\plans\refactored-wishing-kahan.md`)を確定し、承認を得てPhase 1〜3を`implementer`サブエージェントに順に委譲した（CLAUDE.mdの「計画はメインセッション、実装はimplementerに委譲」の役割分担に従った）。

### 何をしたか
**Phase 1（xatlas統合+UV自動生成）**
- xatlas公式リポジトリ(jpcy/xatlas)から`xatlas.h`/`xatlas.cpp`をcurlで直接取得し`include/xatlas.h`/`src/ThirdParty/xatlas.cpp`として配置（改変なし）。`CMakeLists.txt`の`file(GLOB_RECURSE "src/*.cpp")`が自動的に拾うため、**CMakeLists.txt/build.pyは無改修**で済んだ。
- `MeshCube::releaseGPU()`（`src/Instances/MeshCube.cpp`）を、VAO/VBO/EBOのみ解放する`releaseMeshBuffers()`とテクスチャ解放を分離。
- `MeshCube::hasValidUV()`（全頂点のU/V min/maxが閾値未満なら縮退と判定）と`MeshCube::regenerateUV()`（xatlasの`AddMesh`→`Generate`→出力頂点の`xref`で元のPosition/Normal/MatAlphaを複製しつつ新規UVを設定）を追加。
- `MeshCube::loadFromGLB()`でパース直後に`hasValidUV()`が偽なら`regenerateUV()`を自動フォールバック実行。
- `src/Editor/PropertiesPanel.cpp`のMeshCubeブロックに、Terrain Regenerateパターン踏襲の確認ポップアップ付き「UV再生成」手動ボタンを追加（Undoなし）。
- `include/Editor/Localization.hpp`/`src/Editor/Localization.cpp`に新規`LocKey`4件を`Count`直前に追記。

**Phase 2（MeshCube用UV空間Decal合成描画）**
- `Decal`（`include/Instances/Decal.hpp`/`src/Instances/Decal.cpp`）に`UVCenter`(Vector2)/`UVRadius`(float)を追加し、`clone()`/`setProperty()`/`src/Core/SceneLoader.cpp`のYAML保存に反映。
- `include/Editor/CommandHistory.hpp`に`SetDecalUVCommand`（UVCenter+UVRadiusをまとめた1Undo単位）を追加。
- `MeshCube::collectUVDecals(maxCount=8)`を実装。子の`Decal`をFace無視で先着順に最大8個収集。
- `src/fragment.glsl`に`MAX_DECALS 8`のuniform配列とUV距離ベースのブレンドループを追加（`uDecalCount`が0の他クラス描画には影響しない設計）。
- `MeshCube::draw()`内でuniform配線を完結（`GL_TEXTURE2`〜`9`使用、描画後`uDecalCount`を0にリセットしてテクスチャユニットも`GL_TEXTURE0`に戻す）。**Renderer.cpp/Renderer.hppは一切変更していない**。
- `PropertiesPanel.cpp`のDecalブロックで、親が`MeshCube`かどうかにより既存の`Face`Comboと新規`UVCenter`/`UVRadius`DragFloatを出し分け。

**Phase 3（クリックしてDecal配置）**
- `MeshCube::raycastLocal()`をMöller–Trumbore法で実装。CPU側の`m_cpuVertices`/`m_cpuIndices`に対して直接レイ-三角形交差判定を行い、ヒットした三角形のUVをバリセントリック補間して返す（**PhysX/Physics.cppには一切触れていない**）。
- `DecalPlaceState`（`include/Editor/PropertiesPanel.hpp`）を新設し、`EditorManager`経由で`PropertiesPanel`/`ViewportPanel`（セカンダリビューポート含む）へ配線。
- `PropertiesPanel.cpp`のMeshCubeブロックに「Decal配置モード」チェックボックスを追加。
- `ViewportPanel.cpp`のクリック処理に、既存の`m_picker`分岐と並列する新分岐を追加。選択中インスタンスが`MeshCube`かつDecal配置モードONの場合、クリックのワールドレイをMeshCubeのローカル空間へ変換（逆回転+`Size`で除算）して`raycastLocal()`を呼び、ヒットしたUVで`Decal`を生成し`AddInstanceCommand`経由でUndo対応追加。配置後は自動でモードOFF。

各Phaseはimplementerサブエージェントに実装させ、都度`git diff`で設計との照合レビューを実施。最終的に`python build.py build`で全体ビルド成功を確認した。

### なぜそうしたか
- **xatlas導入方式を「ヘッダ+cppを直接配置」にした理由**: Plan agentの調査で、`cgltf.h`/`stb_image.h`等の既存サードパーティライブラリが同じパターン（`include/`直下にヘッダ、実装は対応する`.cpp`内で`_IMPLEMENTATION`マクロ）で導入されており、かつ`CMakeLists.txt`の`file(GLOB_RECURSE "src/*.cpp")`が`src/`配下を再帰的に拾う（`src/imgui/imgui.cpp`等が実例）と分かったため。xatlasはヘッダ+cppの2ファイルのみのMITライブラリなので、この方式なら**ユーザーが事前承認していたビルドシステム変更すら不要**という、より低リスクな着地点を選べた。
- **uniform配線をMeshCube::draw()内で完結させ、Renderer.cppを変更しない設計にした理由**: 既存の`Cube::draw()`が`colorLoc`/`uvScaleLoc`/`isSurfaceGuiLoc`等を自分自身で`glGetUniformLocation`して設定しており、Renderer.cppは一切関与していないという既存パターンをPlan agentの調査で確認。全描画クラスが単一の共有`shaderProgram`を使う設計（`Renderer.cpp:199`付近）のため、MeshCube固有のDecal情報もMeshCube自身が描画中に完結させるのが最も一貫性が高いと判断した。LiquidCubeの`uIsLiquid`だけがRenderer.cpp外側管理という例外だが、これは頂点シェーダーの波アニメが他クラスの描画にまたがって維持されるべきではないという特殊事情のためで、今回のケースには当てはまらないと判断。
- **Decal配置のUV取得をPhysX/Physics.cppに触れずCPU側で自前実装した理由**: 調査の結果、MeshCubeの物理形状はConvex Hullのみで三角形トポロジを保持しないため、PhysX側からは原理的にUV/三角形情報を取得できないと判明。ビューポートのクリック選択自体も自前のOBBスラブ法でPhysXを使っていなかった。MeshCubeがレンダリング用にCPU側の頂点/インデックス配列(`m_cpuVertices`/`m_cpuIndices`)をそのまま保持していたため、これに対する自前レイ-三角形判定が最もスコープが小さく、PhysXの改修（Triangle Mesh cooking等の大掛かりな変更）を避けられる選択だった。
- **Decalサイズ単位をUV空間で直接指定にした理由（ユーザー選択）**: xatlasのアトラスはchartごとにテクセル密度が異なるため「UV空間の単位で正確なワールド空間サイズ」は原理的に保証できない、というトレードオフを提示した上でユーザーが実装のシンプルさを優先して選択した。小さなロゴ・弾痕デカール程度の用途なら実用上問題ないという判断。

### どういう経緯か
1. Plan modeでExploreエージェントを複数ラウンド並列起動: (a)メッシュ/UV構造とプリミティブのUV生成パターン、(b)Decal/テクスチャ投影の仕組み、(c)spec.mdの該当記述確認（→該当なしと判明）。
2. 現状の制約（MeshCubeはUV無しGLBで全頂点U=V=0になる、DecalはFaceベースでMeshCubeには概念自体が無い）を把握した上で、AskUserQuestionでスコープ（Decal個別配置まで含む）とxatlas導入可否を確認。
3. 追加でExploreエージェント2体を並列起動: (a)エディターのDecal配置フロー・レイキャスト機構（→クリックでFace判定する仕組みは存在せず、ビューポート選択は自前OBB判定でPhysXを使っていない、MeshCubeの物理はConvex Hullのみと判明）、(b)PropertyRegistry・Decal/MeshCubeのシリアライズパターン（→両クラスとも未移行=手書き維持と判明）。
4. Decalサイズ単位・同時表示数上限をAskUserQuestionで確認した上で、収集した調査結果全てをPlanエージェントに渡して詳細実装設計を依頼。Plan agentが「releaseGPU()のテクスチャ/バッファ解放責務分離が必要」「シェーダーuniform配線はRenderer.cppでなくMeshCube::draw()内で完結すべき」「テクスチャユニットの衝突回避(GL_TEXTURE0/1が既存用途で専有済み)」等、8点の設計判断ポイントと未解決事項を報告。
5. 未解決事項のうち影響が大きい2点（UV再生成ボタンのUndo要否、Decal配置モードの継続挙動）を追加AskUserQuestionで確認し、他の軽微な判断（xatlas導入方式、regenerateUV()の責務分割、UVCenterのシリアライズキー形式等）はメインセッション側で妥当な既定値を決定。
6. 最終計画を`refactored-wishing-kahan.md`に記述しExitPlanMode、承認を得た。
7. Phase 1〜3を順にimplementerサブエージェントへ委譲。各Phase完了後`git diff`で設計との照合レビューを実施し、Phase 2で発見した軽微な冗長コード（後述）はメインセッション側で直接修正。全Phase完了後`python build.py build`で最終ビルド成功を確認。

### 試して失敗した/計画から変更した点
- **xatlas導入はユーザーが「ビルドシステム変更を伴う」前提で承認していたが、実際にはCMakeLists.txt/build.pyの変更が一切不要だった**。既存の`file(GLOB_RECURSE "src/*.cpp")`が新規配置した`.cpp`を自動的に拾う設計になっていたため。承認内容より安全側に倒せる発見だったので、そのまま無改修の方式を採用した（ユーザーへの再確認は「より低リスクな選択」のため省略）。
- **Phase 2実装で、implementerが`ImGui::IsItemActivated()`をウィジェット呼び出しの前後両方に置く冗長なコードを書いていた**（前方の呼び出しは無関係な直前のアイテムを見てしまうdead code）。実害はない（後方の正しい呼び出しが実際のUndo記録を担っていた）が、レビューで発見しメインセッション側で該当4行を削除して整理した。CLAUDE.mdの役割分担（「メインセッションが自分でEdit/Writeしてよいのは実装成果物の微修正のみ」）の範囲内の対応。

### 未解決・保留
- **xatlas出力のV軸反転（`v.V = 1.0f - uv[1]/height`）は既存GLBロード時の慣習に合わせて実装したが、実機での市松模様テクスチャ等を使った目視検証は未実施**。プロジェクトの方針（`Recubin.exe`の自動起動によるスクリーンショット検証は禁止、[[feedback_no_gui_smoketest]]）によりビルド確認までに留めているため、次回起動確認時にUVの向きが正しいか要確認。
- **UVアトラスのchart間でのDecalサイズの歪み**は既知のトレードオフとしてユーザー合意済みだが、実際にどの程度歪んで見えるかは実機未検証。
- **`regenerateUV()`によるUVアトラスのseam分割で頂点が複製された場合の`MeshCube::getConvexVertices()`（Convex Hull物理形状生成）への影響は理論検証のみで実機未検証**。複製頂点は同じPosition値の重複点として渡るだけで、Convex Hullアルゴリズム自体は内部で自然に重複除去するはずだが、性能・安定性への実質的な影響は未確認。
- **Decal配置モードは「事前に選択中のMeshCube」に対してのみ機能し、シーン内の任意のMeshCubeを都度自動判定してヒットさせる汎用ピッキングツールではない**（選択→モードON→対象メッシュ表面をクリック、という手順が必要）。この挙動は計画・実装指示の前提として扱ったが、UXとして直感的か（例えば「選択せずクリックしたら自動でその下のMeshCubeに配置」を期待するユーザーがいないか）は未確認。
- 8個を超えるDecalが1つのMeshCubeに追加された場合、9個目以降は`collectUVDecals(8)`で静かに無視される（警告UIなし、ユーザー選択による仕様）。エディター上で「上限に達している」ことに気づく手段が無い点は将来的な改善余地として残っている。

### 暗黙仕様の発見（spec.mdに無い挙動）
- **全描画クラス（Cube/MeshCube/Terrain/LiquidCube等）は単一の共有`shaderProgram`（`Renderer.cpp`内で一度だけ`glUseProgram`）を使っており、クラス固有のuniform設定は各クラスの`draw()`メソッド自身が`glGetUniformLocation`で解決して行う**のが基本パターン（`Cube::draw()`の`colorLoc`/`isSurfaceGuiLoc`等が先例）。Renderer.cpp側から外部管理されるのはLiquidCubeの`uIsLiquid`のみの例外で、これは頂点シェーダーの波アニメというクラスを跨いだ特殊事情による。新しくクラス固有のシェーダー機能を追加する場合、Renderer.cppを触らずそのクラスの`draw()`内で完結させるのが既存規約に沿う。
- **テクスチャユニットは`GL_TEXTURE0`=各描画の`ourTexture`（毎描画で切替）、`GL_TEXTURE1`=`shadowMap`（メインループ先頭で固定）と既に専有されている**。新規に複数テクスチャを扱う機能（今回のDecal等）は`GL_TEXTURE2`以降を使い、描画後は`GL_TEXTURE0`に戻す後始末をしないと、次に描画される他オブジェクトのテクスチャバインドを壊す可能性がある。
- **`Decal`と`MeshCube`はどちらも`PropertyRegistry`に未移行で、`setProperty()`/`clone()`/YAML保存が全て手書き**（[[property-schema-registry]]で触れた「意図的に手書き維持するクラス」の実例が2つ増えた形）。新規プロパティ追加時は自動生成の恩恵を受けられず、3箇所（フィールド宣言・`setProperty`・シリアライズ）を手動で同期させる必要がある。
- **`MeshCube`の物理形状はConvex Hullのみで、コードベース全体で`PxTriangleMesh`関連APIの使用が一切無い**。そのためMeshCube表面の三角形単位の情報（UV、法線の面単位の違いなど）が必要な機能は、PhysXクエリでは原理的に取得不可能で、レンダリング用に保持しているCPU側の頂点/インデックス配列に対して自前実装するしかない。
- **`src/Editor/SceneHierarchyPanel.cpp`の`renderInsertMenu`/`tryInsertInstance`は動的なクラスレジストリ列挙ではなく、クラスごとに手書きされたメニュー項目の羅列**（v2.0ネットワーク基盤セッションで発見した内容の再確認）。この設計により「Decalは右クリックメニューから追加すると常にFace::Front固定で生成される」という制約があり、UV空間配置（Face無関係）を実現するには、メニュー経由ではなく今回実装したような別の生成経路（ビューポートクリック）が必要だった。

---
