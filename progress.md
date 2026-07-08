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

---

## 2026-07-07 テストプレイ中のシーン読み込み確認ポップアップ

### 指示内容
readme.mdのTodo「シーンファイルをテストプレイ中に読み込もうとしたら、ポップアップありで終了するか確認とってからすぐシーンを切り替えられるようにする」を実装。

### 何をしたか（`include/Editor/EditorManager.hpp` / `src/Editor/EditorManager.cpp`のみ変更）
- 既存の挙動を調査した結果、ツールバーの「Load」ボタンは元々モードガード無しで`pendingLoadPath`に直接代入していたが、`main.cpp`側のリロード処理（`ed->isEditMode()`必須）がテストプレイ中は素通りするため、確認もなく「Stopを手動で押すまで無反応に見える」状態だった。「Open Scene」メニュー項目・Ctrl+Oは`isEditMode()`ガードで完全にブロックされていた。
- `EditorManager::requestSceneLoad(const std::string& path)`を新設し、Open Sceneメニュー・Ctrl+O（`openSceneDialog()`経由）・ツールバーLoadボタンの3箇所を統一。Editモード中は従来通り即`pendingLoadPath`に代入、Play/Pauseモード中は確認ポップアップ（`m_showPlayLoadConfirm`/`m_pendingPlayLoadPath`）を開くだけに変更。
- `renderPlayLoadConfirmDialog()`を新設（`renderSaveDialog()`と同じImGuiモーダルパターン）。「終了して読み込む」を押すと`mode = EditorMode::Edit`にした上で`pendingLoadPath`をセット。`main.cpp`側は`mode`変更を次フレームで検知し、既存のPlay→Edit遷移処理（スナップショット復元）に続けて同フレーム内で`pendingLoadPath`処理が走るため、`main.cpp`は一切変更せずに「終了確認→即シーン切り替え」を実現できた。
- 「Open Scene」メニュー項目の`isEditMode()`ガードも撤去（AskUserQuestionでユーザーに確認し「解放する」を選択）。Ctrl+Oは`handleEditorShortcuts()`自体が`isEditMode()`のときしか呼ばれない既存の仕組みのため、今回はスコープ外として据え置き。

### なぜそうしたか
- `main.cpp`のPlay→Edit遷移＋pendingLoadPath処理の順序（同フレーム内で連続実行）を確認した上で、`mode`を`Edit`に変えてから`pendingLoadPath`を設定するだけで「即切り替え」が成立すると判断し、`main.cpp`側への変更を避けた（CLAUDE.mdのスコープ厳守原則）。
- Stopボタンの完全な後処理（ビューポートフォーカス切り替え等）は再現せず、`ed->mode = EditorMode::Edit`＋`controlMode = Free`のみに留めた。これは`main.cpp`内の安全装置（`consumeSafetyHaltRequest()`）が同じ最小パターンをすでに使っており、それに倣った。

### 未解決・保留
- Ctrl+Oショートカットは引き続きEditモード中のみ有効（`handleEditorShortcuts()`自体がEditモード限定で呼ばれる既存構造のため）。
- 実機でのポップアップ表示・シーン切り替えの確認はユーザーに委ねる（GUI自動スモークテスト禁止のためビルド成功の確認までに留めた）。

---

## 2026-07-08 3Dビューポート直接ドラッグ移動がUndo履歴に積まれないバグ修正

### 指示内容
readme.mdに新規追加されたTodo「間違えてPositionを変更してしまった場合にCtrl+Zを押しても、dirty判定にはなっているものの何も反映されない問題の修正」を調査・修正。

### 何をしたか（`include/Editor/ViewportPanel.hpp` / `src/Editor/ViewportPanel.cpp`のみ変更）
- ユーザーへのヒアリングで「3Dビューポートでキューブを直接クリック&ドラッグして移動（Gizmoハンドルではない自由移動ドラッグ）」が再現操作で、「Ctrl+Zを押すと移動が戻らず、直前の追加コマンドの取り消し（削除）が実行されるように見える」という具体的な症状を確認。
- `ViewportPanel::onRender()`を調査した結果、自由移動ドラッグのUndo記録ロジック（`wasDragging`/`m_isDraggingSelected`の比較でドラッグ開始/終了を検出し`m_freeDragEntries`をキャプチャ→`MultiGizmoCommand`として記録）に同一フレーム内順序のバグを発見。クリック検出ブロック（`ImGui::IsMouseClicked(0)`時）が`m_isDraggingSelected`をtrueにした**その同じフレーム内で後から**`wasDragging = m_isDraggingSelected`を読んでいたため、「前フレームの値」のつもりが「今フレームで既に更新済みの値」になってしまい、ドラッグ開始時の`false→true`遷移を一度も検出できずbeforeキャプチャが空のままになっていた。結果、ドラッグ終了時も`m_freeDragEntries.empty()`のためコマンドが記録されず、自由移動ドラッグは見た目上位置を変えるだけでUndoスタックに一切積まれていなかった。
- 同ファイル内のGizmoハンドル操作（`m_wasUsingGizmo`）は同種の判定を、ブロック終端で明示的に`m_wasUsingGizmo = isUsingGizmo;`と更新する永続メンバ経由で正しく実装しており、これを参考にした。
- `ViewportPanel.hpp`に新規メンバ`bool m_wasDraggingSelected`を追加し、`ViewportPanel.cpp`側で`wasDragging`の参照元を`m_isDraggingSelected`から`m_wasDraggingSelected`に変更、ブロック終端（自由移動ドラッグ検出の直後、「Moveモード自由移動」セクションの手前）で`m_wasDraggingSelected = m_isDraggingSelected;`を追加して次フレーム用に保存するようにした。

### なぜそうしたか
- 既存の`m_wasUsingGizmo`パターン（同一ファイル内で既に正しく機能している同種のドラッグ開始/終了検出）と全く同じ手法を踏襲することで、最小限の変更で根本原因（同一フレーム内での前フレーム値の汚染）を解消できると判断した。`m_freeDragEntries`のキャプチャ/記録ロジック自体や、Gizmoハンドル側の処理には一切触れていない。

### どういう経緯か
1. readme.mdのTodo文言だけでは再現操作が特定できなかったため、AskUserQuestionで「編集方法」「Ctrl+Z後の数値の挙動」「再現条件」の3点をユーザーに確認。
2. 「3Dビューポートで直接クリック&ドラッグ」「新規作成キューブでも再現」「コマンド履歴が壊れて追加の取り消し(削除)が実行される」という回答を得て、自由移動ドラッグのUndo記録コードに絞って調査。
3. `CommandHistory`/`SetVec3Command`/Propertiesパネルの各Position編集経路（テキスト入力・DragFloat3スライダー）は全て問題なくコマンドを記録しており、自由移動ドラッグ経路のみに絞り込めた。
4. `ViewportPanel.cpp`の該当ブロックを行単位で追い、`wasDragging`の読み取りタイミングが同一フレーム内での`m_isDraggingSelected`書き込みより後になっている点を特定し、Plan modeで原因と修正方針をまとめてユーザー承認を得てから実装。
5. ビルド成功を確認。

### 未解決・保留
- 実機での動作確認（新規キューブ作成→直接ドラッグ移動→Ctrl+Zで位置が戻ることの確認）はユーザーに委ねる（GUI自動スモークテスト禁止のため）。
- readme.mdの当該Todoに「ほかにもある可能性あり」との注記があったが、今回はGizmoハンドル操作・Propertiesパネル編集経路は問題なしと確認済み。他のプロパティ（Rotation/Sizeの自由ドラッグ等）で同種の記録漏れがないかは今回のスコープ外（自由移動ドラッグはPositionのみ変更する経路のため対象外）。

### 暗黙仕様の発見（spec.mdに無い挙動）
- **`m_isDraggingSelected`は「クリックした瞬間のフレーム」に即座にtrueへ更新される設計**（`ImGui::IsMouseClicked(0)`時の当該フレーム内で完結）。同一フレーム内でこの値を「前フレームの状態」として読もうとすると、更新後の値を読んでしまう順序依存のバグになりやすい。前フレームの値が必要な場合は`m_wasUsingGizmo`のように専用の永続メンバをブロック終端で明示的に更新するパターンを使う必要がある。この教訓は今後同種の「開始/終了エッジ検出」コードを書く際に注意が必要。

---

## 2026-07-08 ドラッガー実装（移動範囲クランプ）+ ImGuizmo移動モード「白い球」バグ修正

### 指示内容
readme.mdに新規追加された3項目をまとめて対応：
1. 「ドラッガーの実装」（Cubeサイズ＋軸制限で移動範囲を計算する／ドラッグ軸ごとに不要軸を除去する処理／xz・xy・yzドラッグ時に該当軸を無効化）
2. 「制限ボックス外に出ないようにクランプ処理を実装」
3. 「ImGuizmoの移動モードの白い球が動いてしまうバグを修正する」

### 何をしたか
**A. ドラッガー拡張（`src/Editor/ViewportPanel.cpp`）**
- 既存の「Moveモード自由移動」（選択キューブを直接クリック&ドラッグしてサーフェスに追従させる機能。`castRaySurface()`でヒットした面の法線軸`hitAxis`を固定し、残り2軸をレイと平面の交点で自由に動かす）を調査した結果、「hitAxis固定＝不要軸の除去」自体は既に実装済みだったが、自由な2軸に移動範囲の制限が一切なく、サーフェスの端を越えてどこまでもドラッグできる状態だったと判明。
- `newPos`計算直後・`teleportTo`呼び出し前に、hitAxis以外の2軸について `surfPos[i] ± surfHalf[i] ∓ movHalf[i]`（サーフェスの端から移動物の半サイズ分内側）を範囲としてクランプする処理を追加。サーフェスより移動物が大きく範囲が反転する場合はサーフェス中央に固定するフォールバックも追加。
- 既存の`collisionFit`（他オブジェクトとの任意衝突回避）は、このクランプの後段に従来通り適用されるよう順序を維持（サーフェス端の制約と、他物体との衝突回避を分離）。

**B. ImGuizmo「白い球」バグ修正（`src/imgui/ImGuizmo.cpp`）**
- `src/Editor/EditorManager.cpp:803`の`ImGuizmo::GetStyle().CenterCircleSize = 0.0f;`（TRANSLATEモード中心ハンドルを非表示にする設定）を発見。
- `src/imgui/ImGuizmo.cpp`の`GetMoveType()`内、screen判定（`MT_MOVE_SCREEN`、中心の2軸同時移動ハンドルの当たり判定）が`CenterCircleSize`とは無関係な固定±10pxの`mScreenSquareMin/Max`のみで判定しており、描画は消えているのに当たり判定だけ生き残っていたと特定。
- `GetMoveType()`のscreen判定に`gContext.mStyle.CenterCircleSize > 0.0f`の条件を追加し、非表示状態なら当たり判定も無効化されるようにした。

### なぜそうしたか
- ユーザーヒアリングで「ドラッガー」は既存の自由移動ドラッグの拡張であること、移動範囲は「乗っているサーフェスのサイズ・端」を基準にすることを確認済みだったため、新規の別ドラッグ手段を作らず既存ブロックへのクランプ追加に留めた。
- `src/imgui/ImGuizmo.cpp`は`temp_libs/ImGuizmo/ImGuizmo.cpp`とのdiffで、SCALE/TRANSLATE負方向ハンドル追加など既にプロジェクト側で独自パッチが入っているベンダーコピーだと確認できたため、今回のGetMoveType()修正もこの前例に倣った。
- 同じ`CenterCircleSize`はSCALE中心ハンドル・ROTATE_SCREENリングの見た目にも使われており理論上同種の問題がありうるが、ユーザーの指示は「移動モード」限定だったためTRANSLATE(`GetMoveType()`)のみを修正し、他は対象外とした。

### どういう経緯か
1. readme.mdの3項目について、AskUserQuestionで「既存自由移動ドラッグの拡張か新規ドラッグ手段か」「移動範囲の基準は何か」を確認し、「既存拡張・サーフェス基準」と回答を得た。
2. 「白い球」バグについても、当初は「毎フレーム変化する行列をImGuizmoに渡すことでSCALE同様の内部参照ずれが起きているのでは」という仮説を立てたが、確証がなかったためAskUserQuestionで症状を確認したところ「過去に非表示にしたはずが実際はまだ動作している」という全く別の原因（描画と当たり判定の不整合）と判明。
3. `grep`で`CenterCircleSize`を検索し、`EditorManager.cpp:803`での0.0f設定と、`ImGuizmo.cpp`の`GetMoveType()`のscreen当たり判定コードを突き合わせて根本原因を特定した。
4. Plan modeで両修正をまとめて計画・承認を得てから実装、ビルド成功を確認。

### 未解決・保留
- 実機での動作確認（サーフェス上でキューブを端までドラッグして止まること、ImGuizmo中心の見えないハンドルが反応しなくなること）はユーザーに委ねる（GUI自動スモークテスト禁止のため）。
- `CenterCircleSize`を使うSCALE中心ハンドル・ROTATE_SCREENリングにも同種の「描画は消えているが当たり判定は生きている」問題が理論上ありうるが、今回はスコープ外（ユーザー指示が「移動モード」限定だったため）。

### 暗黙仕様の発見（spec.mdに無い挙動）
- **`src/imgui/ImGuizmo.cpp`はベンダー配布そのままではなく、プロジェクト側で既に独自パッチ（SCALE/TRANSLATE負方向ハンドル追加等）が入ったコピー**（`temp_libs/ImGuizmo/ImGuizmo.cpp`という素のベンダーコピーが別途残っており、diffで差分を確認できる）。このため今回のようにImGuizmo自体の挙動を直す必要がある場合、`src/imgui/ImGuizmo.cpp`を直接編集するのがこのプロジェクトの前例に沿ったやり方になる。
- **ImGuizmoの`Style::CenterCircleSize`は「描画の見た目」のみを制御し、`GetMoveType()`のscreenハンドル当たり判定（`mScreenSquareMin/Max`、固定±10px）とは独立している**。見た目を0にして非表示にしても当たり判定は生きたままになるため、ImGuizmoの視覚要素を「隠す」場合は当たり判定側も併せて確認する必要がある。同じ罠がSCALE中心ハンドル・ROTATE_SCREENリングにも当てはまる可能性がある。

---

## 2026-07-08 Select/Move/Resize/Rotateボタンのトグル化（無操作モード追加）

### 指示内容
エディターツールバーのSelect/Move/Resize/Rotateボタンについて、アクティブなボタンをもう一度押したら「無操作」（クリックしても選択すら変わらない、カメラ操作のみ）になるようにしてほしいという追加依頼。

### 何をしたか（`include/Editor/ViewportPanel.hpp` / `src/Editor/ViewportPanel.cpp` / `src/Editor/EditorManager.cpp`のみ変更）
- `ViewportPanel`に新規メンバ`bool toolNone`を追加。既存の`selectOnly`/`gizmoOp`による4状態（Select/Move/Resize/Rotate、常にどれか1つがアクティブ）に対し、5つ目の状態として追加。
- `isSelectMode()`/`isMoveMode()`/`isResizeMode()`/`isRotateMode()`全てに`!toolNone`の条件を追加し、`toolNone`が真の間はどれも偽になるようにした。新規`isNoToolMode()`（toolNoneそのもの）と`isGizmoMode()`（Move/Resize/Rotateのいずれか）のクエリも追加。
- クリック処理の入り口（`ViewportPanel::onRender()`内、「クリック処理: 選択 & ドラッグ開始」ブロックの条件）に`!isNoToolMode()`を追加し、無操作中はクリックしても選択もドラッグ開始も一切発生しないようにした。ボックス選択（`isSelectMode()`をチェック済み）や自由移動ドラッグ（`isMoveMode()`をチェック済み）は、上記の`is*Mode()`変更により自動的に無操作中は無効化される。
- ギズモのオーバーレイ描画条件を`!isSelectMode()`から`isGizmoMode()`（Move/Resize/Rotateのいずれか）に変更。旧条件のままだと`toolNone`中に`isSelectMode()`が偽になりギズモが誤って表示されてしまうため。
- `EditorManager::renderToolbar()`の4ボタンを、「既にアクティブなモードのボタンを押したら`toolNone = true`、そうでなければ従来通りそのモードに切り替えて`toolNone = false`」というトグル処理に書き換えた。

### なぜそうしたか
- ユーザーヒアリングで「無操作」は本当に何もしない（クリックしても選択すら変わらない、カメラ操作のみ）状態であることと、メイン・セカンダリ両方のビューポートに適用することを確認済み。
- `renderToolbar()`は`GetFocusedViewport()`で取得した`activeViewport`（メインまたはセカンダリ）に対して同じボタンコードを使う構造だったため、`ViewportPanel`側にトグル状態を持たせるだけで自然にメイン・セカンダリ両方に適用される。
- 状態表現を丸ごとenumに置き換える大きな書き換えは避け、既存の`selectOnly`/`gizmoOp`を残したまま`toolNone`を追加する最小変更にした（CLAUDE.mdのスコープ厳守方針）。

### 未解決・保留
- 実機での動作確認（同じボタンをもう一度押すと無操作になり、クリックしても選択が変わらないこと）はユーザーに委ねる（GUI自動スモークテスト禁止のため）。

---

## 2026-07-08 Weldグループ壊れ修正 + BaseCubeクローン漏れ修正 + 浮力の面分散化

### 指示内容
readme.mdの「物理エンジン関係」Todo3件を対応するよう依頼された:
1. マテリアルが違うと溶接できない問題の修正(水上でテストした結果)
2. マテリアルがBaseCube系でクローン時に保存されていない(他もある可能性あり)
3. 浮力の改善(一点集中→面に分散、アセンブリ剛体も対応)

Plan modeで、CLAUDE.mdの指示によりExplore/PlanエージェントもTaskツールも使わず、Grep/Readで自分で調査してから実装した。

### 何をしたか

**1. Weldグループ壊れ問題（`src/Core/Physics.cpp`のみ変更）**
- `Physics::recreateActor()`冒頭に、対象キューブの`actor`が他の`cubes`エントリーと共有されているか（＝Weld compoundのメンバーか）を判定するロジックを追加。共有されていれば（`sharedGroup.size() > 1`）、個別のremove/release/createActorではなく`rebuildGroup()`にグループ全体の再構築を委譲して`return`。共有されていない場合は既存の単独アクター処理のまま変更なし。

**2. BaseCube系クローン漏れ修正（8ファイル: `Cube.cpp`/`Truss.cpp`/`Seat.cpp`/`MeshCube.cpp`/`Sphere.cpp`/`Cylinder.cpp`/`TriangularPrism.cpp`/`LiquidCube.cpp`）**
- 各`clone()`の`CanCollide`コピー行の直後に、`material`/`MassDensity`/`CastShadow`/`Unlit`/`UseTriplanar`/`TextureScale`のコピーを追加（6項目×8ファイル）。`LockFlags`はスクリプト/UIから設定されない内部専用フィールド（Humanoid初期化のみが設定）のため対象外とした。

**3. 浮力の面分散化（`src/Core/Physics.cpp`の`applyBuoyancy()`のみ変更）**
- `BUOYANCY_SAMPLE_RES = 3`（3×3×3＝27点）のグリッドサンプリング定数を追加。
- 合計浮力（`weightedV` = Σ液体重なり体積×Density）は既存のAABB重なり体積計算のまま変更せず、力の適用点だけをキューブのAABB内27点のグリッドサンプル点に分散。各サンプル点がどの液体AABB内にあるかを判定し、重み比率で`weightedV`を配分して`physx::PxRigidBodyExt::addForceAtPos()`で各点に加える（既存の重心一点`addForce`を置き換え）。ダンピング計算（`frac`）は既存のAABB重なり体積ベースのまま変更なし。

ビルド確認: `python build.py build`で成功（Recubin.exe/RecubinEngine.exe/RecubinTest.exe全て生成）。

### なぜそうしたか

- **Weldバグの原因特定の経緯**: readme.mdの文言「マテリアルが違うと溶接できない」を字面通り受け取ると、Weld作成時にマテリアルの違いで拒否される分岐を探すことになるが、Physics.cpp/Weld.cpp/rebuildGroupを調査してもそのような分岐は一切見つからなかった（PxMaterialは形状ごとに独立して割り当てられ、Weld自体はマテリアルを一切参照しない）。AskUserQuestionでユーザーに実際の症状を確認したところ、「新規Weldの作成自体は成立するが、既に溶接済みのキューブのマテリアルを変更すると暗黙的に解除される」という、readmeの文言よりも正確な症状が判明した。
- **根本原因**: `BaseCube::setAnchored()`/`setMaterial()`/`setMassDensity()`が共通で呼ぶ`Physics::recreateActor()`が、`cube->actor`を常に「このキューブ専用の単独アクター」と仮定していた。Weldで結合されたキューブは`cube->actor`がcompound（複数キューブ共有のPxRigidDynamic）を指すため、素朴なremove/release/createActorはcompound全体を破棄し、変更対象のキューブだけ新しい単独アクターを作り直す（＝グループから抜ける）。さらに他メンバーの`cube->actor`（BaseCube自身が持つ生ポインタ）は誰も更新しないため、解放済みcompoundを指すダングリングポインタとして残る潜在的UAFバグも同時に発見した。この不具合はsetMaterialに限らずsetAnchored/setMassDensity/setSize（リサイズ）にも共通する経路であることも確認した。
- **修正方式の選択**: 新規にグラフ探索やWeld専用の特別処理を書くのではなく、既存の`rebuildGroup()`（Weld作成時に使われているグループ再構築処理）にそのまま委譲する方式を選んだ。`rebuildGroup()`は各メンバーの`material`/`MassDensity`/`Anchored`/`LockFlags`を都度読み直してcompoundを作り直し、`cubes`/`m_constraints`（Weldの`m_compound`、Rope/Rod/Motorの再生成）の同期まで内部で完結するため、追加コード量が最小で済み、既存のテスト済みロジックを再利用できる。
- **クローン漏れの調査**: LiquidCube::clone()でmaterialが未コピーであることに気づいた後、他の全BaseCube派生クラスのclone()を確認したところ、7クラス全てで同一パターン（Name/Color/Anchored/CanCollide/cframeのみコピー）が繰り返されていることが判明。readmeの「他もあるかもしれない」はこれを指しており、材質だけでなくMassDensity/CastShadow/Unlit/UseTriplanar/TextureScaleも全クラスで漏れていた。
- **修正スタイルの選択**: 8ファイルに同じ6行を追加する方式（重複コードの反復）を採用し、BaseCubeに共有ヘルパーメソッドを新設する案は取らなかった。CLAUDE.mdの「最適化・リファクタは明示的に指示された場合のみ行う」「より良い設計に勝手に置き換えない」という原則と、既存コード自体が「各クラスで同じ5行を個別にコピーする」スタイルを既に採用していたことに合わせた判断。
- **浮力の分散方式の選択**: グリッドサンプリング方式（3×3×3点）と、水没体積重心への単一点適用方式の2案をAskUserQuestionでユーザーに提示。単一点でも重心からずれた位置に適用点を置けば理論上はトルクが発生し転覆問題自体は解消するが、readmeの「面に分散させ」という文言に字義通り忠実ではない。ユーザーはグリッドサンプリング方式を選択した。
- **合計力の計算方法**: グリッドサンプリングによる離散化誤差が浮力の総量（Archimedesの原理との一致）に影響しないよう、合計力自体は既存の連続的なAABB重なり体積計算（`weightedV`）をそのまま使い、グリッドは「どこに配分するか」の比率計算のみに使う設計にした。これにより総浮力の正確性を保ったまま、水没箇所に応じた偏り（トルク）だけを新たに発生させられる。
- **アセンブリ対応への追加コード不要と判断した理由**: `applyBuoyancy()`は元々`cubes`内の各BaseCubeエントリー単位でループしており、Weld compoundのメンバーであっても各キューブが自分自身の位置情報を使って独立に浮力計算・力印加を行う構造だった。したがってaddForceAtPos化により各メンバーが自分の位置でグリッドサンプリングして力を加えるだけで、アセンブリ全体としても自然に分散が成立するため、特別な分岐追加は不要と判断した。

### どういう経緯か

1. readme.mdの物理エンジン関係Todo3件の計画を依頼される。IDE選択範囲（readme.mdの浮力改善Todoの詳細説明部分）も併せて提示された。
2. Plan mode開始。CLAUDE.mdの指示（計画はセッション自身が行う、サブエージェントに外注しない）に従い、Explore/PlanエージェントもTaskツールも使わずGrep/Readで自分で調査。
3. まずreadme.md/progress.mdを読んで前提を確認。次にPhysics.cpp/BaseCube.cpp/Weld.cpp/LiquidCube.cpp/Material.hppを読み込み、3件それぞれの現状コードを把握。
4. Weldバグ（1番目）について、Weld.cpp/rebuildGroup/attachShapeToCompound/getOrCreateMaterial/SceneHierarchyPanel.cpp/PropertiesPanel.cpp/Humanoid.cpp/LuauEngine.cpp/SceneLoader.cppを広く調査したが、マテリアルの違いでWeldを拒否・失敗させる分岐はコード上に一切見つからなかった。
5. これ以上の憶測での実装はリスクが高いと判断し、AskUserQuestionでユーザーに実際の症状を確認。ユーザーから「新規Weld作成は成立するが、既に溶接済みの状態でマテリアルを変更すると暗黙的に解除される」という、より正確な症状とデバッグログが提供された。
6. この情報を元に`BaseCube::setMaterial()`→`Physics::recreateActor()`の経路を再確認し、compound共有を考慮していない実装バグだと特定。
7. 同時にAskUserQuestionで浮力分散方式（グリッドサンプリング vs 単一点）についても確認し、グリッドサンプリング方式が選ばれた。
8. clone()漏れについては、LiquidCube.cppを読んだ時点でmaterial未コピーに気づき、他の全BaseCube派生クラスのclone()実装（Cube/Truss/Seat/MeshCube/Sphere/Cylinder/TriangularPrism）を横断的に確認して同一パターンの漏れを確認。
9. PxRigidBodyExt.hに`addForceAtPos`が既に存在することを確認し、Vector3.hppの演算子（componentwise `operator*`含む）が浮力分散の実装に必要な演算をサポートしていることも確認した上でプランを作成。
10. プランファイルを作成しExitPlanModeでユーザー承認を得てから実装開始。
11. 実装順序: `Physics::recreateActor()`修正→8ファイルのclone()修正→`applyBuoyancy()`のグリッドサンプリング化→`python build.py build`でビルド確認。ビルド成功。

### 未解決・保留

- 実機確認（Weldグループの安定性、クローンでのプロパティ保持、船の転覆耐性）は、GUI自動スモークテスト禁止方針のため、ビルド成功の確認までに留めた。次回セッション冒頭で以下をユーザーに確認する必要がある:
  - 2キューブをWeldで結合→Cube側のMaterialType/Anchored/MassDensityを変更→グループが分離せずクラッシュ/警告も出ないか
  - MaterialType変更済みのBaseCube系を複製し、複製後もMaterial/MassDensity/CastShadow等が保持されているか
  - 複数キューブのWeldアセンブリ（船）をLiquidCubeに傾けて浮かべ、以前より転覆しにくく傾きから復元するか
- 浮力のグリッド解像度`BUOYANCY_SAMPLE_RES = 3`（27点）は固定値で実装した。実機確認の結果、精度不足（トルクが弱すぎる）や逆に重すぎる（パフォーマンス懸念）と判断された場合は調整が必要。
- 調査中に気づいた別事象: Playモードの開始・停止を繰り返すと`[SceneLoader] Weld "Weld": cube not found`という警告が毎回コンソールに出力される。今回のTodo3件とは無関係と判断し対応しなかったが、原因は未調査。将来readme.mdに追加Todoとして起票する候補。
- `setSize()`（リサイズ）経路も`recreateActor()`を通るため今回の修正の恩恵を受けるはずだが、リサイズ動作自体の実機確認は行っていない。

### 試して失敗した/やめた方法

- **Weldバグの原因を「マテリアルの違いによるWeld拒否ロジック」だと決め打ちして実装を始める案**: readme.mdの文言を字面通り解釈すると自然にこの仮説に至るが、Weld.cpp/Physics.cpp/rebuildGroup等を実際に読んでもそのような分岐はコード上に存在しなかった。もしここで憶測のまま「material一致チェックを追加する」といった見当違いの実装をしていたら、本当の原因（recreateActorのcompound非対応）を見逃していた。CLAUDE.mdの「迷ったら実装せず質問する」方針に従いユーザーに確認したことで、正しい原因に到達できた。
- **浮力を水没体積の重心1点に単一のaddForceAtPosで適用する案**: 転覆問題自体は解決できる（理論上は正しいトルクが発生する）が、readmeの「力を面に分散させ」という要求に字義通りではないため、より計算コストの高いグリッドサンプリング案と両方提示してユーザーに選んでもらった。

### 暗黙仕様の発見（spec.mdに無い挙動）

- **`Physics::recreateActor()`は`cube->actor`を常に「このキューブ専用の単独アクター」と暗黙的に仮定しており、Weld compoundの共有アクターケースを考慮していなかった**。`BaseCube::setAnchored()`/`setMaterial()`/`setMassDensity()`は全てこの関数を経由するため、Weldグループのメンバーに対してこれらのプロパティを変更すると、暗黙的にそのキューブがグループから外れ、かつ他メンバーの`actor`ポインタがダングリングになるという副作用があった。今後`recreateActor()`を呼ぶ新しいプロパティセッターを追加する際は、この関数が既にcompound共有を考慮済み（`rebuildGroup`に委譲する分岐がある）という前提で問題ないが、直接`scene->removeActor`/`release`を呼ぶような新規コードを書く場合は、同じ「共有アクターかどうか」の判定が必要になる。
- **全BaseCube派生クラスのclone()実装は、`Name`/`Color`/`Anchored`/`CanCollide`/`cframe`のみを個別にコピーする手書きスタイルで統一されており、BaseCubeに新しいプロパティを追加した際にclone()側への反映を強制する仕組みが無い**。今回`material`/`MassDensity`/`CastShadow`/`Unlit`/`UseTriplanar`/`TextureScale`の6項目が7クラス全てで漏れていたのはこのため。今後BaseCubeに新規プロパティを追加する場合、この8ファイルのclone()全てに手動で反映を追加する必要があることを意識する必要がある（BaseCubeは意図的に手書き維持されているクラスのため、PropertyRegistryへの移行対象外であり、この手作業は今後も続く）。
- **`Physics::applyBuoyancy()`は`cubes`配列を「BaseCubeエントリー単位」でループしており、Weld compoundのメンバーは自分のactor（共有compound）に対して個別に力を加える構造になっている**。この構造のおかげで、今回の浮力分散化はアセンブリ（Weld）に対しても特別な分岐を追加せずに自然に対応できた。今後、浮力以外の「キューブごとの外力」を追加する場合も、同じくcubesエントリー単位でループする限りアセンブリ対応が自動的に得られる設計になっている。

