# 開発進捗ログ

セッションごとの作業記録。新しいセッションを開始する際はまず一番下（最新）のセッションを読むこと。

---

## 2026-07-07 OpenGLパイプライン最適化 + Mac対応準備（IPlatformインターフェース導入）

### 指示内容
readme.mdのTodoリストのうち以下2項目を続けて依頼された:
1. 「OpenGLパイプラインを最適化(必要かは検討の余地あり)」
2. 「Mac対応を開始する」のうち「レンダラーを抽象化する(OS依存をなくし、関数の実装部分で分岐を行う)」「ファイル操作のインターフェイス化」（Metal実装/OpenGL4.1準拠自体はスコープ外、Mac実装は動作確認不要のモックでよいと明示）

いずれもPlan modeで、Explore/PlanエージェントはCLAUDE.mdの指示により使わずGrep/Readで自分で調査してから実装した。

### Part A: OpenGLパイプライン最適化

**何をしたか**（`include/Core/Renderer.hpp` / `src/Core/Renderer.cpp`のみ変更）
- **Uniform locationキャッシュ**: `Renderer::init()`で`shaderProgram`の全uniform location（`view`/`projection`/`viewPos`/`model`/`unlit`/`useTriplanar`/`u_textureScale`/`uTime`/`uIsLiquid`/`useVertexColor`/`ourColor`/`hasShadows`/`lightSpaceMatrix`、および光源8灯分の`uLights[i].*`）を一括取得してメンバ変数にキャッシュ。`renderViewport()`のMain Pass、`renderTerrain()`内で毎フレーム発生していた`glGetUniformLocation`呼び出し（特に光源ループ内の文字列結合）を置き換え。
- **フラスタムカリング**: `extractFrustumPlanes()`/`sphereInFrustum()`をRenderer.cpp内のファイルローカルstaticヘルパーとして追加（Gribb-Hartmann法で6平面抽出）。`renderInst`ラムダ内のCube/Cylinder/TriangularPrism/Sphere/MeshCube/LiquidCube各分岐で、バウンディングスフィア（中心=ワールド位置、半径=`Size.length()*0.5`）が視錐台外なら描画自体をスキップ。
- **不透明/半透明分離**: `setBlendForAlpha`ラムダを追加。`Color.a>=0.999f`のオブジェクトは`GL_BLEND`を無効化して描画。ループを抜けた直後に`GL_BLEND`を再度有効化（`renderClouds`/`renderParticles`が「BLEND常時有効」を前提にしているため）。

**なぜそうしたか**
- スコープはメインカメラパスのBaseCube系のみに限定し、Shadow Pass（別のライト空間frustumが必要でリスク増）とTerrain（既存の距離ベースストリーミングに任せる方が一貫性がある）は対象外とAskUserQuestionで確認済み。インスタンシング/バッチングも設計変更が大きいため対象外。
- バウンディングスフィアを採用したのは、BaseCube系は回転しうるため軸並行AABBだと不正確になるため。対角線の半分を半径とする保守的な球なら安全側に倒せる。

**どういう経緯か**
1. Renderer.cpp/hppを自分で読み込み、Uniform未キャッシュ・フラスタムカリング無し・GL_BLEND常時有効・インスタンシング無しの4点の無駄を発見。
2. AskUserQuestionで対象を3点（Uniform/カリング/ブレンド分離）に絞り込み、実装・ビルド成功。
3. ユーザーが実機確認し「劣化は見受けられなかった、良い修正」と評価。

**未解決・保留**: 特になし（ユーザー確認済み）。

### Part B: Mac対応準備（IPlatformインターフェース導入）

**何をしたか**
- 新規: `include/Util/IPlatform.hpp`（インターフェース、`FileFilter`構造体）、`include/Util/Platform.hpp` + `src/Util/Platform.cpp`（`getPlatform()`シングルトンアクセサ）、`include/Util/WindowsPlatform.hpp` + `src/Util/WindowsPlatform.cpp`（Windows実装）、`include/Util/MockPlatform.hpp`（Mac向けスタブ）。
- 6箇所に重複していたCOMファイルダイアログコードを統合: `src/Editor/EditorManager.cpp`（`openSceneDialog()`・パッケージ出力先フォルダ選択・Loadボタンの3箇所）、`src/Editor/SceneHierarchyPanel.cpp`（`pickFile()`/`pickFolder()`）、`src/Editor/PropertiesPanel.cpp`（`browseFolder()`、`browseFile()`呼び出し10箇所）、`src/Editor/AnimationEditorPanel.cpp`（`openAnimDialog()`）。
- `ShellExecuteW`呼び出し（SceneHierarchyPanel.cpp、PropertiesPanel.cpp）を`getPlatform().revealInFileManager()`に置き換え。
- `SetConsoleOutputCP`/`SetConsoleCP`（main.cpp、game_main.cpp、test_main.cpp）を`getPlatform().setupConsoleUtf8()`に置き換え。
- `src/Core/LuarCompiler.cpp`/`include/Core/LuarCompiler.hpp`の`LoadLibraryA`/`GetProcAddress`/`FreeLibrary`（`HMODULE`）を`getPlatform().loadDynamicLibrary`/`getSymbol`/`freeDynamicLibrary`（`void*`）に置き換え。
- 実際には使われていなかった`<windows26.h>`を`Renderer.hpp`、`EditorManager.hpp`、`ViewportPanel.hpp`、`SecondaryViewportPanel.hpp`、`LuarCompiler.hpp`から削除。`game_main.cpp`/`test_main.cpp`の不要な直接includeも削除。
- `include/Util/FileDialog.hpp`・`src/Util/FileDialog.cpp`を削除（WindowsPlatformに統合されたため）。
- `doc/Util/FileDialog.md` → `doc/Util/IPlatform.md`にリネーム・内容更新、`doc/Util/README.md`のインデックス更新。
- `src/Instances/Sound.cpp`のコメント内「`browseFile()`」表記を「`getPlatform().openFileDialog()`」に更新（削除された関数への言及を修正）。

**なぜそうしたか**
- 「レンダラーを抽象化する」を調査した結果、Rendererクラス自体の描画ロジックは既にOpenGL/GLFWでポータブルであり、真の問題は`Renderer.hpp`など4ヘッダが実際には使っていないのに`<windows26.h>`を巻き込んでいたことだと判明。ここがエンジンのコア部分のクロスプラットフォーム化を阻害していた。
- 「ファイル操作のインターフェイス化」を調査した結果、`browseFile()`相当のCOMダイアログロジックが実は6箇所に重複コピペされている実態を発見。インターフェース化する前にまずこの重複を1箇所（WindowsPlatform）に統合する必要があった。
- 既存の`IInputBackend`/`GLFWInputBackend`、`IEditorManager`/`NullEditorManager`という確立されたパターン（インターフェース+具象クラス）を踏襲し、新規パターンを持ち込まず一貫性を優先した。
- ファイル操作インターフェースの対象範囲をユーザーに確認（ダイアログ系のみ vs コンソール設定・DLLロードも含める）し、広い方（後者）を選択された。
- Mock実装の検証方法もユーザーに確認（コンパイルが通ることのみ確認 vs Windowsビルド上でMockに切り替えられるスイッチを追加）し、後者を選択。CMakeLists.txt/build.pyには触れず、環境変数`RECUBIN_MOCK_PLATFORM`でランタイム切り替えする方式にした（ビルドシステムを触らないというCLAUDE.mdの制約を守りつつ検証手段を用意するため）。
- `IPlatform::openFileDialog`/`saveFileDialog`は単一の`filterName`/`filterSpec`ではなく`std::vector<FileFilter>`で受ける設計にした。理由は次項。
- フォルダピッカーの挙動差異（EditorManager.cpp/PropertiesPanel.cppは`FOS_PATHMUSTEXIST`付き、SceneHierarchyPanel.cppは無し）は、より安全な「PATHMUSTEXIST付き」に統一した。
- `LuarCompiler`のエラーログから`GetLastError()`のエラーコード表示が失われる点は、小さな挙動変化として許容した（実装前にプラン上で明示）。

**どういう経緯か**
1. Plan mode再突入。前回のOpenGL最適化プランとは別タスクのため、プランファイルを上書きして新規作成。
2. `windows26.h`の使用箇所、`browseFile()`や`ShellExecuteW`等の実際のOS依存箇所をGrep/Readで自分で調査。
3. 調査中にCOMダイアログコードの6箇所重複という想定外の事実を発見し、当初の想定（単純にIPlatformでラップするだけ）よりスコープが広がった。
4. AskUserQuestionで2点（ファイル操作インターフェースの対象範囲、Mock検証方法）を確認してから最終プランを作成、承認を得た。
5. 実装順序: IPlatform.hpp→MockPlatform.hpp→WindowsPlatform.hpp/cpp→Platform.hpp/cpp→LuarCompiler→main/game_main/test_main→PropertiesPanel→SceneHierarchyPanel→EditorManager→AnimationEditorPanel→ヘッダの`windows26.h`削除→FileDialog削除→ビルド確認の順で実施。
6. 実装中、`SceneHierarchyPanel.cpp`の`pickFile()`が実は2つのフィルタ（Luau Script/Luar Script）を同時に登録する仕様だったと判明し、`IPlatform`のシグネチャを計画時点の単一`filterName`/`filterSpec`案から`std::vector<FileFilter>`に拡張した。
7. ビルド成功後、`RecubinTest.exe`を通常起動と`RECUBIN_MOCK_PLATFORM=1`起動の両方で実行し出力を比較、完全一致（既存の84 passed/2 failedは無関係な既存差分）を確認。

**試して失敗した/計画から変更した点**
- 当初プランの`IPlatform::openFileDialog(filterName, filterSpec)`という単一フィルタのシグネチャは、実装中に`SceneHierarchyPanel.cpp`の`pickFile()`が2フィルタ必須と判明したため`std::vector<FileFilter>`に拡張した。プラン段階の「代表的なコード例」は必ずしも全呼び出し元を検証済みではないため、実装時に全呼び出し元を洗い出してから確定させる必要がある、という教訓。

**未解決・保留**
- 実際のダイアログ操作（ファイルを開く/保存、フォルダ選択、エクスプローラーで開く）はGUIが絡むため、エディターでの実機確認は未実施。次回セッション冒頭で確認が必要。
- `RECUBIN_MOCK_PLATFORM=1`でのエディター実機起動（ダイアログがクラッシュせず空文字列として扱われるか）も未確認。`RecubinTest.exe`（ヘッドレス）のみでの確認に留まる。
- `CMakeLists.txt`/`build.py`は意図的に触っていないため、実際のMacビルドターゲットはまだ存在しない。将来Mac対応を進める場合はビルドシステム自体の対応が別途必要。
- `launcher/main.cpp`のWindows専用ショートカット生成コード（`CoCreateInstance(CLSID_ShellLink)`等）は今回スコープ外のまま。将来Mac版ランチャーが必要になれば別途対応要。
- `FileLoader.cpp`/`SceneLoader.cpp`/`Sound.cpp`に残る独立した`utf8_to_wstring()`実装（3箇所の重複）は、「呼び出し側への漏れが無い」という理由で今回スコープ外にしたが、コード自体の重複は解消されていない。将来リファクタする余地あり。
- `RecubinTest`実行結果の「2 failed」はFileRef関連の既存テスト失敗で、今回の変更とは無関係と判断したが、原因自体は未調査。

### 暗黙仕様の発見（spec.mdに無い挙動）

- **`Renderer.hpp`/`EditorManager.hpp`/`ViewportPanel.hpp`/`SecondaryViewportPanel.hpp`が`<windows26.h>`をincludeしていたが、実際にはどのヘッダもWin32型（HWND/DWORD等）を使用しておらず完全に不要だった**（`LuarCompiler.hpp`のみ`HMODULE`で正当な使用だったが、今回`void*`化して不要になった）。ヘッダの不要include確認は、実際のシンボル使用箇所をgrepしないと見落としやすい典型例。
- **`fragment.glsl`のアルファ出力は`outAlpha = ourColor.a * MatAlpha`のみで、Decal/Textureのアルファ(`texColor.a`)は最終出力に一切影響しない設計**（2026-07-05セッションで確定済みの仕様）。この仕様のおかげで、フラスタムカリング/ブレンド分離のような「Cube単位でオブジェクトの不透明度を判定する」処理は`Color.a`だけを見れば正しく動作する（テクスチャの透明度を別途考慮する必要が無い）。
- **`browseFile()`という「1箇所に定義された共通関数」が存在するにもかかわらず、実際には6箇所で同じCOMダイアログロジックがコピペされ、共通関数は一部の呼び出し元（main.cpp、PropertiesPanel.cppの一部）からしか使われていなかった**。「共通ヘルパーがあるからといって実際に使われているとは限らない」という点は、今後同種の重複コードを探す際の教訓。
- **`EditorManager.cpp`には同一のシーンファイル読み込みダイアログ処理が2箇所（`openSceneDialog()`メソッドと、Loadボタンのインラインコード）に完全に重複して存在していた**。メソッド化されているのに使われずインライン重複が残るというパターンがあるため、類似の重複を探す際は「同名メソッドの有無」だけでなく「同じロジックパターンのgrep」も必要。

---

## 2026-07-07 Decal名前の勝手な変更 + Decal/Texture/SurfaceGui のUV左右反転修正

### 指示内容
readme.mdのTodoリストのうち以下2項目の修正を依頼された:
1. Decalの名前が勝手に変わる問題の修正
2. Decalがカメラから見て左右反転している問題の修正

実装後、ユーザーから追加で「Top/Bottom面のDecalもまだ左右反転している」という指摘を受け、同種の修正を追加実施。

### 何をしたか

**1. Decal名前の勝手な変更バグ修正**
- 原因: `Decal::setFace()`/`Texture::setFace()`がFace変更のたびに無条件で`Name`を既定名(`Decal_<Face>`/`Texture_<Face>`)で上書きしていた。`PropertiesPanel.cpp`のFaceコンボボックス変更時にこの`setFace()`が呼ばれるため、F2やProperties欄で改名済みのDecal/Textureでも、Faceを変えるたびに名前が消えていた。
- `src/Instances/Decal.cpp`・`src/Instances/Texture.cpp`の`setFace()`を、「現在の`Name`がまだ変更前Faceの既定名と一致する場合のみ新しい既定名に追従し、ユーザーが改名済みなら上書きしない」という条件分岐に変更。
- `SceneLoader.cpp`のYAML読み込み時は元々プロパティ適用後に保存済み`Name`を再上書きする実装になっており（既知のコメントあり）、この修正と競合しないことを確認済み。

**2. Decal/Texture/SurfaceGuiのUV左右反転バグ修正（垂直4面）**
- 原因: `src/Instances/Cube.cpp`の`createCubeVertices()`が全キューブ系（Cube/Truss/Seat等、Named<T,Cube>継承クラス）で共有する唯一のVAOを生成しており、そのUV基底`u`（テクスチャの「右方向」）が、Front/Back/Right/Left全4面で実際のカメラ右方向と逆になっていた（クォータニオンの`getForward`/`getRight`/`getUp`から数値的に検証）。
- `u = Vector3(-nz, 0, nx)` → `Vector3(nz, 0, -nx)`に修正。
- Decal/Textureと同じVAO・同じTexCoordをSurfaceGuiベイクテクスチャの3D面表示も使っているため、`Renderer_GUI.cpp`の`bakeSurfaceGui()`にあった「焼き込みテクスチャの手動左右反転」（根本のUV不整合を打ち消すための辻褄合わせ処理）を削除。あわせて`hitTestSurfaceGui()`のクリック判定用`uAxis`（Front/Back/Right/Left分）と、Xミラー補正(`fboX = (1.0f - texU) * L.w` → `texU * L.w`)も新しいUV基底に合わせて更新。

**3. Top/Bottom面の左右反転追加修正**
- ユーザー指摘を受け再調査。Top/Bottomは視点のyawによって見え方が回転する（floor decal特有の正常な性質）ため、単純な「カメラ右方向との比較」では判定できなかった。代わりに`cross(u, vv)`（UV基底の外積）と面法線の符号を6面全てで比較したところ、修正済みのFront/Back/Right/Leftは全て`cross(u,vv) == +法線`で一貫していたのに対し、Top/Bottomだけ`cross(u,vv) == -法線`と符号が逆で、視点によらない恒常的な鏡像バグだと判明。
- `Cube.cpp`のTop/Bottom分岐の`u`を`(1,0,0)`→`(-1,0,0)`に反転。`Renderer_GUI.cpp`の`hitTestSurfaceGui()`のTop/Bottom用`uAxis`も同様に反転して整合させた。

### なぜそうしたか（判断の経緯）

- **Decal名前バグの修正方針**: 「Faceを変えたら常にリネームする」仕様自体を廃止するのではなく、「まだ既定名のときだけ追従」という条件を追加する方式を選択。Faceを変えてすぐは名前が追従してくれる利便性を残しつつ、ユーザーが改名した後は上書きしない、という両立を狙った。
- **Texture::setFace()も同時に修正するか**: readme.mdのTodoは「Decal」のみ明記だったが、全く同じバグパターンがTexture.cppにも存在したため、AskUserQuestionでユーザーに確認。「同時に修正」を選択されたため対応。
- **UV左右反転の直し方（Decal限定のシェーダーuniform分岐 vs Cube.cpp根本修正）**: 前者はDecalだけに影響を絞れるが、Cube.cppの頂点生成はDecal/Texture/SurfaceGuiベイクで完全に共有されており、後者の方が本質的な修正になる。AskUserQuestionで両案を提示し、ユーザーは「根本のUV基底自体を直す」（Texture/SurfaceGuiへの影響も許容）を選択。CLAUDE.mdの「指示されたファイルだけ触る」原則とはやや緊張関係にあるが、事前に選択肢と影響範囲を明示し承認を得た上での実施。
- **bakeSurfaceGuiの手動左右反転を「削除」した理由**: この反転処理は、UV基底の不整合を打ち消すために後から追加された辻褄合わせだったと判断。根本のUV基底を直した後もこの反転処理を残すと二重反転になり、SurfaceGuiだけ再び逆向きになってしまうため、単純な「両方直す」ではなく「片方（辻褄合わせ側）を消す」必要があった。
- **Top/Bottomの判定にcross(u,vv)を使った理由**: floor/ceiling decalは見る角度（yaw）によって画面上で回転して見えるのが正常な挙動であり、「特定の視点でのカメラ右方向と比較する」だけでは真の鏡像バグと視点依存の回転を区別できない。視点に依存しない不変量として、UV基底の外積と面法線の符号一致（＝右手系として一貫しているか）を全6面で比較する方法を用いた。

### どういう経緯か

1. readme.mdのTodo2件（Decal名前バグ、Decal左右反転）を実装依頼される。
2. `Decal.cpp`/`Decal.hpp`を読み、`setFace()`の無条件Name上書きが名前バグの原因と特定。
3. 左右反転バグは`Cube.cpp`の`createCubeVertices()`のUV基底を数式で検証（クォータニオンの`getForward`/`getRight`/`getUp`を使い、各面を正面から見た時の実際のカメラ右方向を計算）し、垂直4面全てで逆転していると判明。この修正はDecal/Texture/SurfaceGuiが同じ頂点データを共有するため影響範囲が広いと分かり、AskUserQuestionで2問（UV修正方式、Texture::setFaceも直すか）を確認してから実装。
4. ビルド成功を確認し、readme.mdのTodo2件をチェック済みに変更。
5. ユーザーから「Top/Bottomもまだ左右反転している」と追加報告。最初はTop/Bottomにも同じ「カメラ右方向との比較」を適用しようとしたが、Top/Bottomは視点のyawによって画面上の見え方が回転するため、単一の「正しいカメラ右方向」が定義できないことに気づいた（試行錯誤: yaw=0/90/180の3パターンでカメラ右方向を計算したが、それぞれ異なる結果になり、この比較方法では判定不能と判断）。
6. 代わりに`cross(u,vv)`と面法線の符号一致を6面で比較する方法に切り替え、Top/Bottomのみ符号が逆（恒常的な鏡像バグ）と判明。`Cube.cpp`と`Renderer_GUI.cpp`のTop/Bottom分をそれぞれ反転し、ビルド成功。

### 試して失敗した/やめた方法

- **Top/Bottomの反転判定に「特定の視点でのカメラ右方向」を使う方法**: 垂直4面と同じ手法（yawを固定してカメラのforward/right/upを計算し、UV基底のuと比較）をTop/Bottomにも適用しようとしたが、yaw=0では一致、yaw=90では90°回転した関係、yaw=180では不一致という具合に、比較する視点(yaw)によって結果が変わってしまい、この面には使えない手法だと判明。→ **教訓**: 面の法線が視線方向（forward）と平行になりうる面（Top/Bottom）は、視点固定の「カメラ右方向」比較では判定できない。視点に依存しない不変量（外積と法線の符号一致など）で判定する必要がある。

### 未解決・保留

- Decal画像の向き、SurfaceGuiボタンのクリック位置、Top/Bottom面のDecalの見た目について、実機での確認はユーザーに委ねている（GUIアプリの自動スモークテストは方針上禁止のため、ビルド成功の確認までに留めた）。
- `doc/Instances/Decal.md`・`doc/Instances/Texture.md`に残っている可能性のある「setFace()はNameを常に既定名に上書きする」という趣旨の記述は、今回の挙動変更に合わせて更新できていない（ドキュメント更新は指示されていないため未対応、次回関連作業時に確認要）。
- readme.mdは会話の途中でユーザー側（またはlinter）により再構成され、Todoがカテゴリ別（設計関係/エディター関係/物理エンジン関係/レンダリング関連）に整理し直され、完了済み項目の行自体が削除された。今回チェック済みにした2行はこの再構成で既に無くなっているため、追加対応は不要と判断。

### 暗黙仕様の発見（spec.mdに無い挙動）

- **`Cube.cpp`の`createCubeVertices()`は、Cube本体だけでなくTruss/Seat（`Named<T,Cube>`継承）を含む全キューブ系で共有される唯一のVAOであり、Decal/Texture/SurfaceGuiベイクの3D面表示は全てこの同一頂点データのUV座標に依存している**。この頂点生成を1箇所直すと、意図せずとも必ずこの3系統全てに影響が波及する。今後この関数を触る際は、影響範囲としてこの3系統を毎回セットで確認する必要がある。
- **`bakeSurfaceGui()`にあった「テクスチャの手動左右反転」処理は、実際にはUV基底の不整合を打ち消すための辻褄合わせだった**。根本原因（`createCubeVertices`のUV基底）を直す際は、この種の「対症療法として後から追加された補正コード」を一緒に取り除かないと二重反転で再び壊れる。同様のパターン（片方の修正が別の補正コードと組み合わさって「たまたま正しく見えていた」）が今後も見つかる可能性がある。
- **面の法線が視線方向と平行になりうる面（Top/Bottom、あるいはSkyboxの上下面など）では、「特定視点でのカメラ右方向」を基準にした左右判定が原理的に使えない**（yawによって画面上の見え方が回転するため）。この種の面のUV向きを検証する際は、`cross(u,vv)`と法線の符号一致という視点非依存の不変量を使うと判定できる。

