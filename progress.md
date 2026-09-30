# 開発進捗ログ

セッションごとの作業記録。新しいセッションを開始する際はまず一番下（最新）のセッションを読むこと。

---


## 2026-10-01 キャラクター周りのバグ修正（物理・座標・ジャンプ）

### 1. 何をしたか
- **MaintainVelocity(Torque)が摩擦で減衰する問題**: `Box3DPhysicsBackend.cpp` の `applyMaintainedVelocities()` に、ステップ後の角速度差分ぶんの回転を重心回りに加える補正を追加。YawForce(Torque+MaintainVelocity)を持つキャラは `CharacterRig::collectR6Bodies()` の全ボディを同じ剛体回転で補正する。
- **System.AutoSpawnPlayerCharacter**: `main.cpp` のエディタPlay開始処理で判定を追加（game_main.cppには元からあった）。
- **物理書き戻しのずれ**: `Box3DPhysicsBackend.cpp` の `syncCubeWorldCFramePreservingAttachments()` の「子なしCube近道」が、ワールド値をローカルとして `commitCFrame` に渡していた。座標親のローカルへ変換するよう修正。
- **ModelのPositionプロパティ**: `Spatial.cpp` のPosition/Rotation/CFrame登録を整理。`assignLocalCFrameProperty()` を追加し、Model(Tool除く)への代入はPivotTo相当（原点が指定姿勢になるよう子孫ごと剛体移動）にした。プロパティ欄・YAML・Undo/Redo・Lua(`luaSet`)・`setWorldPosition` すべて同じ経路。Tool/Model以外は従来どおり。
- **Model原点の重心追従**: `Model::syncPivotToCentroid()` / `Model::syncPivotsToCentroid()` を追加し、`Physics::update()` の末尾から毎物理更新で呼ぶ。動的(非Anchored)BaseCubeを含むModelのみ、重心差0.01stud以上で原点を重心へ更新（子のワールド姿勢は保存）。
- **Lua `Model:MoveOrigin(cframe)` 追加**（`LuauEngine.cpp` `model_move_origin_closure`、Dispatch登録）: 原点だけ動かし子のワールド姿勢は保つ。
- **ジャンプ再開が遅い問題 + 足が触れていないのにジャンプできる問題**: `Humanoid.cpp` に `isLegTouching()` を追加し、`jump()` の許可と `updateGroundHover()` のジャンプ再許可(`m_groundJumpRearmPending`解除)に使用。`Physics::isTouchActive()`（IPhysicsBackend/Box3D実装）を追加。

### 2. なぜそうしたか（判断理由）
- **摩擦無視をRoot速度の再設定ではなく姿勢補正にした理由**: 速度はステップ前設定・ステップ後復元だが、姿勢は摩擦で減った速度で積分されるため回転量が足りなかった。ステップ後に不足ぶんの回転を足す方式（近似）を採用。
- **補正をRootのみ→リグ全体に変更した理由**: Rootだけ`SetTransform`で回すと、Motor6Dのばね(Frequency 10)で他パーツが遅れて追従し、向きのずれ・ぐわんぐわん揺れる回転が出た（ユーザー報告）。YawForceの診断ブロックが元々全R6ボディを回す設計だったのに合わせた。
- **Modelプロパティ代入をPivotTo相当にした理由**: 従来の`setCFrame`は「子のワールド位置を保存」する設計で、親だけ動いて子が動かないように見えた。ギズモは別経路で子ごと動くため不一致。重心追従を入れた後は「Position=重心」なので、代入=PivotToが自然。Undoで元に戻るよう「原点基準のdelta」で移動する実装にした（pivot基準だと未同期モデルでUndoがずれる）。
- **Luaの`Model.Position`も同じ挙動にした理由**: 重心追従があると「子の位置を保つ代入」は次の物理更新で打ち消され無意味になるため。代わりに原点だけ動かす`MoveOrigin`を別メソッドで提供（ユーザー承認済み）。
- **ジャンプ判定: A)幾何近似を小さくする vs B)センサー接触に一本化 → Bを採用**: 足のTouchedを接続するゲームで、幾何近似(足裏下0.3stud)とTouchedセンサー重なりにズレがあると「触れていないのにジャンプ→Touchedを回避」が可能。Touched発火元(足センサーの`m_activeTouches`)と判定を一致させた。壁対策は足場を`normal.y>=0.5`のシェイプキャスト結果に限定。`CanTouch=false`の足場/BaseCube以外(地形)はTouchedが出ないので従来の幾何判定（ユーザー指定）。
- **センサー常時生成はしなかった**: Touchedが接続されていない足にはずるが成立しないため、既存の「観測時のみセンサー生成」のまま、未接続なら幾何判定にフォールバックする設計にした。

### 3. 経緯
- 最初: 「Force/TorqueのMaintainVelocityが摩擦で減る」→ 姿勢補正で解決。→ AutoSpawnPlayerCharacterがエディタで無視される → 修正。
- 「Model/StarterCharacter下の座標がずれる」→ 使い捨てプローブ(test_main.cppに一時コード→復元)で再現。Model配下のパーツが物理後にY+16ずれる（書き戻しのlocal/world取り違え）を特定。続いて「プロパティ代入でModelが動かない」を修正。
- その後「dream.yamlのリグだけ異常回転」→ 調査。当初は再現せず、データ不整合（左右腕脚の位置とMotor6D C0が逆、SpawnLocationなしで高さ100から落下しRagdoll）を指摘。ユーザーが直しても改善せず、症状（歩行中に向きがカメラとずれて揺れる）から、Rootのみ補正が原因と判断→リグ全体補正で解決（ユーザー確認済み）。
- ジャンプ再開が遅い → 再許可条件を緩和(HipHeight+1.0) → 「足が触れないのにジャンプできるずる」指摘 → 足接触判定へ変更 → さらに「Touchedを回避できる/壁でジャンプできる」指摘 → センサー一本化＋上向き法線。
- 最後に、Model原点の重心追従、Lua代入のPivotTo化、`MoveOrigin`追加。

### 試して失敗・やり直した方法
- Root速度を再設定するだけ/Rootのみ`SetTransform`補正 → 後者は歩行中の揺れの原因。
- 足接触の幾何判定: ①足裏から+0.2上に薄い箱を置いて下キャスト → めり込み/ちょうど接触は初期接触扱いでヒットしない。②足の上端から下向きキャスト＋tolerance 0.3に変更して解決（デフォルトリグは足が床に1stud沈む）。この間`CharacterHoverRegression`が一時6件落ち、②で解消。
- `JUMP_REARM_TOLERANCE`（HipHeight+1.0）は暫定策だったため、足接触判定導入時に削除。
- プローブ作成時の罠: sed/pythonの`\n`エスケープで文字列に改行が入りビルド失敗。`SceneLoader::loadScene`のルートは`System`ではなくフラット形式のバッグ(Instance)。

### 4. 未解決・保留
- `PhysicsMigrationRegression`は実行ごとに失敗数が揺れる（7〜12件）。今回の変更とは無関係だが原因未調査。`HumanoidRigCollisionRegression`は既存失敗で、重心追従後に診断値が変化（失敗のまま、関係未調査）。
- `Replication.cpp:948` がネットワークでCubeのローカルCFrameを送信し受信側でワールドとして適用（`setWorldCFrame`）。Model配下のパーツで同種のずれが出るはず（未修正）。
- `ViewportPanel.cpp:1370`（Fキーフォーカス）がローカル座標をカメラ位置に使用（未修正）。
- 重心は固定(Anchored)BaseCubeも含む。動く部分だけの重心にするかは未決定。
- `User::buildCharacterModel` はStarterCharacter自身の座標を無視して原点Modelにcloneする（相対配置は保たれるが、StarterCharacterを原点以外に置いた場合の意図は未確認）。
- `MoveOrigin`の名前は私が決めた。ドキュメント・エディタ補完定義（RCBN.luah等）は未更新。
- ジャンプ: 片足だけTouched接続している場合、未接続足の幾何判定では許可しない仕様にした（抜け道防止）。実機のマウス操作・可変フレームでの最終確認は未実施。足裏の許容0.3stud(`FOOT_CONTACT_TOLERANCE`)は暫定値。
- dream.yamlは左右の腕脚位置とMotor6D C0が逆（ユーザー側で修正済みと報告）。SpawnLocationなし＋ImpactRagdollThreshold 45 だと高所落下でRagdollする。

### 5. 暗黙仕様の発見（spec.mdにない）
- `Spatial::setCFrame`/`setWorldCFrame`は「子孫のワールド姿勢を保存」する。つまり親を動かしても子は動かない（spec.mdのWeld節「親が移動しても子は追従しない」と整合するが、座標系節には明記なし）。ギズモ経路(`applyEditorWorldCFrame`)だけは子ごと動く非対称があった。
- `Instance::setParent`（addChild）はSpatialのワールド姿勢を保存する（ローカルを書き換える）。クローン/ロードは`cloneForest`/`attachDeserializedChild`でローカルを復元している。
- Humanoid::move()のYawForce制御は`errorDegrees/dt`（上限8rad/s）の即時目標速度方式。摩擦で回転が鈍るバグが隠れていたため、修正後は旋回が本来の速度で速くなる。
- 足のTouchedセンサーは「リスナー接続時のみ」生成（`BaseCube::isTouchObserved()`）。センサー重なりは足裏が床に触れるまで発火せず、幾何近似より数フレーム遅い。
- `CharacterRig`のデフォルトリグは、HipHeight 3で足が床に約1stud沈んだ状態で静止する。
- Model(Tool除く)はPlay中、動的な子を含む限り原点が重心に自動更新される。Lua/プロパティ/YAMLの`Position`代入はPivotToとして子ごと動く。
